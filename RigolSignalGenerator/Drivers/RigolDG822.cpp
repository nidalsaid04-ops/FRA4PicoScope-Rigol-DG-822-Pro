#include "RigolDG822.h"

// IVI/NI VISA header, found through the project's VISA include directory.
#include <visa.h>
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <thread>

// Link the VISA import library that matches the build architecture.
// The library directory comes from the project settings (VisaRoot).
#if defined(_WIN64)
#pragma comment(lib, "visa64.lib")
#else
#pragma comment(lib, "visa32.lib")
#endif

static_assert(sizeof(ViSession) == sizeof(unsigned long),
              "RigolDG822 stores ViSession handles as unsigned long");

namespace
{
    constexpr double DG822_MIN_SINE_HZ = 1.0e-6;   // 1 uHz
    constexpr double DG822_MAX_SINE_HZ = 25.0e6;   // 25 MHz

    constexpr double HIGHZ_MIN_VPP = 0.002;        // 2 mVpp
    constexpr double HIGHZ_MAX_VPP = 20.0;         // 20 Vpp

    constexpr double LOAD50_MIN_VPP = 0.001;       // 1 mVpp
    constexpr double LOAD50_MAX_VPP = 10.0;        // 10 Vpp

    // Upper bound on drained :SYST:ERR? entries, so a misbehaving
    // instrument can never trap us in an endless loop.
    constexpr int MAX_ERROR_QUEUE_READS = 32;

    bool Succeeded(ViStatus status)
    {
        return status >= VI_SUCCESS;
    }

    void DebugLog(const std::string& text)
    {
        OutputDebugStringA(("[RigolDG822] " + text + "\n").c_str());
    }

    std::string Trim(std::string s)
    {
        const char* ws = " \t\r\n";
        const size_t last = s.find_last_not_of(ws);
        if (last == std::string::npos)
            return std::string();
        s.erase(last + 1);
        s.erase(0, s.find_first_not_of(ws));
        return s;
    }

    std::string UpperAscii(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) -> char
            {
                return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A')
                                              : static_cast<char>(c);
            });
        return s;
    }

    std::string NarrowAscii(const std::wstring& w)
    {
        std::string s;
        s.reserve(w.size());
        for (wchar_t c : w)
        {
            if (c > 0x7F) // VISA resource names are ASCII.
                return std::string();
            s.push_back(static_cast<char>(c));
        }
        return s;
    }

    bool RawWrite(ViSession session, const std::string& command)
    {
        if (session == VI_NULL)
            return false;

        std::string line = command;
        if (line.empty() || line.back() != '\n')
            line.push_back('\n');

        ViUInt32 written = 0;
        const ViStatus st = viWrite(session,
                                    reinterpret_cast<ViBuf>(const_cast<char*>(line.data())),
                                    static_cast<ViUInt32>(line.size()),
                                    &written);

        const bool ok = Succeeded(st) && written == static_cast<ViUInt32>(line.size());
        if (!ok)
            DebugLog("Write failed: " + command);
        return ok;
    }

    bool RawQuery(ViSession session, const std::string& command, std::string& response)
    {
        response.clear();

        if (!RawWrite(session, command))
            return false;

        // VI_SUCCESS_MAX_CNT means more of the reply is still pending.
        // Read it all so a reply tail never becomes the answer to the next query.
        constexpr size_t maxResponseBytes = 64 * 1024;
        for (;;)
        {
            unsigned char buffer[1024];
            ViUInt32 readCount = 0;
            const ViStatus st = viRead(session, buffer, sizeof(buffer), &readCount);

            if (!Succeeded(st) || readCount > sizeof(buffer) ||
                response.size() + readCount > maxResponseBytes)
            {
                response.clear();
                DebugLog("Read failed for query: " + command);
                return false; // Caller must discard the session.
            }

            response.append(reinterpret_cast<const char*>(buffer), readCount);

            if (st != VI_SUCCESS_MAX_CNT || (!response.empty() && response.back() == '\n'))
                break;

            if (readCount == 0)
            {
                response.clear();
                return false;
            }
        }

        response = Trim(response);
        return !response.empty();
    }

    bool ConfigureVisa(ViSession session, ViUInt32 timeoutMs)
    {
        return Succeeded(viSetAttribute(session, VI_ATTR_TMO_VALUE, timeoutMs)) &&
               Succeeded(viSetAttribute(session, VI_ATTR_TERMCHAR, '\n')) &&
               Succeeded(viSetAttribute(session, VI_ATTR_TERMCHAR_EN, VI_TRUE));
    }
}

// -------------------------------------------------------------------------------------------------
// Static helpers (also used by instrument discovery)
// -------------------------------------------------------------------------------------------------

bool RigolDG822::QueryIdn(const std::string& resource, uint32_t timeoutMs, std::string& idn)
{
    idn.clear();

    ViSession rm = VI_NULL;
    if (!Succeeded(viOpenDefaultRM(&rm)))
        return false;

    bool ok = false;
    ViSession session = VI_NULL;
    if (Succeeded(viOpen(rm, const_cast<ViRsrc>(resource.c_str()), VI_NULL, timeoutMs, &session)))
    {
        ok = ConfigureVisa(session, timeoutMs) && RawQuery(session, "*IDN?", idn);
        viClose(session);
    }

    viClose(rm);
    return ok;
}

bool RigolDG822::IsCompatibleIdn(const std::string& idn)
{
    // "Rigol Technologies,DG822 Pro,<serial>,<firmware>"
    std::istringstream fields(UpperAscii(idn));
    std::string maker, model;
    if (!std::getline(fields, maker, ',') || !std::getline(fields, model, ','))
        return false;

    model = Trim(model);
    return Trim(maker).find("RIGOL") != std::string::npos &&
           (model == "DG822" || model == "DG822 PRO" || model == "DG822PRO");
}

// -------------------------------------------------------------------------------------------------
// Construction / init string
// -------------------------------------------------------------------------------------------------

RigolDG822::RigolDG822(const wchar_t* resourceId)
    : m_resourceId(resourceId ? resourceId : L"")
{
}

RigolDG822::~RigolDG822()
{
    Terminate();
}

bool RigolDG822::ParseInitString(const wchar_t* initString)
{
    // "CH=1;LOAD=HIGHZ;TIMEOUT=10000" - ';' or ',' separated, any order, all optional.
    if (!initString || initString[0] == L'\0')
        return true;

    std::wstring input(initString);
    std::replace(input.begin(), input.end(), L',', L';');

    std::wstringstream ss(input);
    std::wstring token;
    while (std::getline(ss, token, L';'))
    {
        token.erase(std::remove_if(token.begin(), token.end(),
                                   [](wchar_t c) { return std::iswspace(c) != 0; }),
                    token.end());
        std::transform(token.begin(), token.end(), token.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });

        if (token.empty())
            continue;

        const size_t eq = token.find(L'=');
        if (eq == std::wstring::npos)
            return false;

        const std::wstring key = token.substr(0, eq);
        const std::wstring value = token.substr(eq + 1);

        if (key == L"CH" || key == L"CHANNEL")
        {
            if (value == L"1")
                m_channel = 1;
            else if (value == L"2")
                m_channel = 2;
            else
                return false;
        }
        else if (key == L"LOAD")
        {
            if (value == L"HIGHZ" || value == L"HIZ" || value == L"INF" || value == L"INFINITY")
                m_highZ = true;
            else if (value == L"50" || value == L"50OHM" || value == L"50OHMS")
                m_highZ = false;
            else
                return false;
        }
        else if (key == L"TIMEOUT")
        {
            // Digits only, and short enough that stoul can never throw.
            if (value.empty() || value.size() > 5 ||
                value.find_first_not_of(L"0123456789") != std::wstring::npos)
                return false;

            const unsigned long v = std::stoul(value);
            if (v < 100 || v > 60000)
                return false;
            m_timeoutMs = static_cast<uint32_t>(v);
        }
        else
        {
            return false;
        }
    }

    return true;
}

// -------------------------------------------------------------------------------------------------
// VISA session
// -------------------------------------------------------------------------------------------------

bool RigolDG822::OpenVisa()
{
    CloseVisa();

    const std::string resource = NarrowAscii(m_resourceId);
    if (resource.empty())
        return false;

    ViSession rm = VI_NULL;
    if (!Succeeded(viOpenDefaultRM(&rm)))
    {
        DebugLog("viOpenDefaultRM failed");
        return false;
    }
    m_defaultRM = rm;

    ViSession session = VI_NULL;
    if (!Succeeded(viOpen(rm, const_cast<ViRsrc>(resource.c_str()), VI_NULL, m_timeoutMs, &session)))
    {
        DebugLog("viOpen failed for " + resource);
        CloseVisa();
        return false;
    }
    m_session = session;

    if (!ConfigureVisa(session, m_timeoutMs))
    {
        DebugLog("Configuring VISA attributes failed for " + resource);
        CloseVisa();
        return false;
    }

    return true;
}

void RigolDG822::CloseVisa()
{
    if (m_session != VI_NULL)
    {
        const ViSession session = static_cast<ViSession>(m_session);

        // Standard USB488 Go-To-Local, also dropping REN. NOTE: DG822 Pro
        // firmware 00.02.01 accepts this but stays in remote mode; on that
        // firmware only the front-panel key returns it to local (verified on
        // hardware; the programming guide documents no local command).
        const ViStatus status = viGpibControlREN(session, VI_GPIB_REN_DEASSERT_GTL);
        if (!Succeeded(status))
        {
            ViChar description[256] = {};
            viStatusDesc(session, status, description);
            DebugLog(std::string("Go-To-Local failed: ") + description);
        }

        viClose(session);
        m_session = VI_NULL;
    }

    if (m_defaultRM != VI_NULL)
    {
        viClose(static_cast<ViSession>(m_defaultRM));
        m_defaultRM = VI_NULL;
    }
}

bool RigolDG822::Write(const std::string& command)
{
    return RawWrite(static_cast<ViSession>(m_session), command);
}

bool RigolDG822::Query(const std::string& command, std::string& response)
{
    return RawQuery(static_cast<ViSession>(m_session), command, response);
}

RigolDG822::Completion RigolDG822::CheckCompleted()
{
    std::string reply;
    if (!Query("*OPC?", reply))
        return Completion::CommError;

    if (reply != "1" && reply != "+1")
    {
        DebugLog("Unexpected *OPC? reply: " + reply);
        return Completion::CommError;
    }

    // Drain the whole error queue. Reading only one entry would leave older
    // errors behind to fail the next, perfectly valid, command.
    Completion result = Completion::Ok;
    for (int i = 0; i < MAX_ERROR_QUEUE_READS; ++i)
    {
        if (!Query(":SYST:ERR?", reply))
            return Completion::CommError;

        const size_t comma = reply.find(',');
        if (comma == std::string::npos)
            return Completion::CommError; // Not a SCPI error reply.

        const std::string code = Trim(reply.substr(0, comma));
        if (code == "0" || code == "+0" || code == "-0")
            return result;

        DebugLog("Instrument error: " + reply);
        result = Completion::InstrumentError;
    }

    return result;
}

bool RigolDG822::Execute(const std::string& command)
{
    if (!Write(command))
    {
        Terminate();
        return false;
    }

    switch (CheckCompleted())
    {
    case Completion::Ok:
        return true;

    case Completion::InstrumentError:
        // The instrument rejected the command but communication is fine:
        // make the output safe and keep the session for the next attempt.
        if (!Write(Ch(":OUTP") + ":STAT OFF") || CheckCompleted() == Completion::CommError)
            Terminate();
        return false;

    case Completion::CommError:
    default:
        // Unknown state: best-effort output OFF, then close.
        Terminate();
        return false;
    }
}

std::string RigolDG822::Ch(const char* prefix) const
{
    return std::string(prefix) + std::to_string(m_channel);
}

// -------------------------------------------------------------------------------------------------
// ExtSigGen
// -------------------------------------------------------------------------------------------------

bool RigolDG822::Initialize(const wchar_t* initString)
{
    Terminate();

    m_channel = 1;
    m_highZ = true;
    m_timeoutMs = kDefaultTimeoutMs;

    if (!ParseInitString(initString))
        return false;

    // No resource from FRA4PicoScope: use the first DG822 that answers on USB.
    if (m_resourceId.empty())
    {
        const std::vector<std::wstring> found = FindDG822Resources();
        if (found.empty())
            return false;
        m_resourceId = found.front();
    }

    if (!OpenVisa())
        return false;

    // *CLS first: an old error left in the queue by front-panel use would
    // otherwise make the first completion check fail.
    if (!Write("*CLS") || !Query("*IDN?", m_idn) || !IsCompatibleIdn(m_idn))
    {
        CloseVisa();
        return false;
    }

    // Start with the output off, Vpp units, and the configured load.
    // HIGHZ is the default because FRA setups usually drive a high-impedance
    // scope input / DUT; use LOAD=50 for a terminated input.
    const bool configured =
        Write(Ch(":OUTP") + ":STAT OFF") &&
        Write(Ch(":OUTP") + ":LOAD " + (m_highZ ? "INF" : "50")) &&
        Write(Ch(":SOUR") + ":VOLT:UNIT VPP") &&
        CheckCompleted() == Completion::Ok;

    if (!configured)
    {
        Terminate();
        return false;
    }

    m_ready = true;
    return true;
}

bool RigolDG822::Terminate(void)
{
    bool ok = true;

    if (m_session != VI_NULL)
    {
        // Best effort: after a communication failure nothing guarantees that
        // these commands reach the instrument.
        ok = Write(Ch(":OUTP") + ":STAT OFF") && ok;

        // Return the front panel and touch screen to manual use.
        ok = Write(":SYST:KLOC OFF") && ok;
        ok = Write(":SYST:TOUC ON") && ok;

        // Let the instrument process the final commands before the USB
        // session is torn down.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    m_ready = false;
    CloseVisa();
    return ok;
}

bool RigolDG822::Ready(void)
{
    return m_ready && m_session != VI_NULL;
}

double RigolDG822::MaxAmplitudeVpp() const
{
    return m_highZ ? HIGHZ_MAX_VPP : LOAD50_MAX_VPP;
}

double RigolDG822::MinAmplitudeVpp() const
{
    return m_highZ ? HIGHZ_MIN_VPP : LOAD50_MIN_VPP;
}

bool RigolDG822::SetSignalGenerator(double amplitudeVpp, double offsetV, double frequencyHz)
{
    if (!Ready())
        return false;

    if (!std::isfinite(amplitudeVpp) || !std::isfinite(offsetV) || !std::isfinite(frequencyHz))
        return false;

    if (frequencyHz < DG822_MIN_SINE_HZ || frequencyHz > DG822_MAX_SINE_HZ)
        return false;

    if (amplitudeVpp < MinAmplitudeVpp() || amplitudeVpp > MaxAmplitudeVpp())
        return false;

    // RIGOL: 2 * |offset| + amplitude must not exceed the amplitude limit.
    if (2.0 * std::abs(offsetV) + amplitudeVpp > MaxAmplitudeVpp() + 1e-12)
        return false;

    // One command sets waveform, frequency, amplitude, offset and phase.
    std::ostringstream apply;
    apply.imbue(std::locale::classic()); // SCPI always needs a '.' decimal point
    apply << std::setprecision(std::numeric_limits<double>::max_digits10)
          << Ch(":SOUR") << ":APPL:SIN "
          << frequencyHz << "," << amplitudeVpp << "," << offsetV << ",0";

    // Always reassert ON: a cached flag cannot see front-panel changes.
    // Command completion is not analog settling; FRA4PicoScope applies its
    // own settling delay.
    return Execute(apply.str()) && Execute(Ch(":OUTP") + ":STAT ON");
}

bool RigolDG822::DisableSignalGenerator(void)
{
    if (m_session == VI_NULL)
        return false;
    return Execute(Ch(":OUTP") + ":STAT OFF");
}

bool RigolDG822::GetNearestFrequency(double frequency, double* nearestFrequency)
{
    if (!nearestFrequency || !std::isfinite(frequency) ||
        frequency < DG822_MIN_SINE_HZ || frequency > DG822_MAX_SINE_HZ)
    {
        return false;
    }

    // 1 uHz programming resolution, kept inside the valid range.
    const double rounded = std::round(frequency * 1.0e6) / 1.0e6;
    *nearestFrequency = std::clamp(rounded, DG822_MIN_SINE_HZ, DG822_MAX_SINE_HZ);
    return true;
}

bool RigolDG822::GetMaxFrequency(double* maxFrequency)
{
    if (!maxFrequency)
        return false;
    *maxFrequency = DG822_MAX_SINE_HZ;
    return true;
}

bool RigolDG822::GetMinFrequency(double* minFrequency)
{
    if (!minFrequency)
        return false;
    *minFrequency = DG822_MIN_SINE_HZ;
    return true;
}

namespace
{
    bool ValidSweepRange(double minFrequency, double maxFrequency)
    {
        return std::isfinite(minFrequency) && std::isfinite(maxFrequency) &&
               minFrequency >= DG822_MIN_SINE_HZ && maxFrequency <= DG822_MAX_SINE_HZ &&
               minFrequency <= maxFrequency;
    }
}

bool RigolDG822::GetMaxAmplitudeVpp(double* maxAmplitudeVpp, double minFrequency, double maxFrequency)
{
    if (!maxAmplitudeVpp || !ValidSweepRange(minFrequency, maxFrequency))
        return false;
    *maxAmplitudeVpp = MaxAmplitudeVpp();
    return true;
}

bool RigolDG822::GetMinAmplitudeVpp(double* minAmplitudeVpp, double minFrequency, double maxFrequency)
{
    if (!minAmplitudeVpp || !ValidSweepRange(minFrequency, maxFrequency))
        return false;
    *minAmplitudeVpp = MinAmplitudeVpp();
    return true;
}

bool RigolDG822::IsDDS(double* dacFrequency, uint8_t* phaseAccumulatorSize)
{
    // The DG800 Pro uses RIGOL SiFi II rather than a conventional DDS.
    // Returning false stops FRA4PicoScope from quantizing frequencies with
    // a DAC-clock / phase-accumulator model.
    if (dacFrequency)
        *dacFrequency = 0.0;
    if (phaseAccumulatorSize)
        *phaseAccumulatorSize = 0;
    return false;
}

bool RigolDG822::SupportsDcOffset(void)
{
    return true;
}
