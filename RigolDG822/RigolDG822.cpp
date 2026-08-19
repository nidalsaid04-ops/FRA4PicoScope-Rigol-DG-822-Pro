#include "RigolDG822.h"

#include <visa.h>
#pragma comment(lib, "C:\\Program Files (x86)\\IVI Foundation\\VISA\\WinNT\\lib\\msc\\visa32.lib")

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <thread>
#include <chrono>

namespace
{
    constexpr double DG822_MIN_SINE_HZ = 1.0e-6;   // 1 uHz
    constexpr double DG822_MAX_SINE_HZ = 25.0e6;   // 25 MHz

    constexpr double HIGHZ_MIN_VPP = 0.002;         // 2 mVpp
    constexpr double HIGHZ_MAX_VPP = 20.0;          // 20 Vpp

    constexpr double LOAD50_MIN_VPP = 0.001;        // 1 mVpp
    constexpr double LOAD50_MAX_VPP = 10.0;         // 10 Vpp

    bool VisaSucceeded(ViStatus status)
    {
        return status >= VI_SUCCESS;
    }

    std::string Trim(std::string s)
    {
        while (!s.empty() &&
               (s.back() == '\r' || s.back() == '\n' ||
                s.back() == ' '  || s.back() == '\t'))
        {
            s.pop_back();
        }

        size_t first = 0;
        while (first < s.size() &&
               (s[first] == ' ' || s[first] == '\t' ||
                s[first] == '\r' || s[first] == '\n'))
        {
            ++first;
        }

        if (first > 0)
            s.erase(0, first);

        return s;
    }

    std::string UpperAscii(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) -> char
            {
                if (c >= 'a' && c <= 'z')
                    return static_cast<char>(c - 'a' + 'A');
                return static_cast<char>(c);
            });
        return s;
    }

    std::wstring UpperWide(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](wchar_t c) -> wchar_t
            {
                return static_cast<wchar_t>(std::towupper(c));
            });
        return s;
    }

    std::string NarrowAscii(const std::wstring& w)
    {
        std::string s;
        s.reserve(w.size());

        for (wchar_t c : w)
        {
            // VISA resource names are ASCII.
            if (c < 0 || c > 0x7F)
                return std::string();

            s.push_back(static_cast<char>(c));
        }
        return s;
    }

    std::wstring WidenAscii(const std::string& s)
    {
        std::wstring w;
        w.reserve(s.size());

        for (unsigned char c : s)
            w.push_back(static_cast<wchar_t>(c));

        return w;
    }

    bool RawWrite(ViSession session, const std::string& command)
    {
        if (session == VI_NULL)
            return false;

        std::string line = command;
        if (line.empty() || line.back() != '\n')
            line.push_back('\n');

        ViUInt32 written = 0;
        ViStatus st = viWrite(
            session,
            reinterpret_cast<ViBuf>(const_cast<char*>(line.data())),
            static_cast<ViUInt32>(line.size()),
            &written);

        return VisaSucceeded(st) &&
               written == static_cast<ViUInt32>(line.size());
    }

    bool RawQuery(ViSession session,
                  const std::string& command,
                  std::string& response)
    {
        response.clear();

        if (!RawWrite(session, command))
            return false;

        unsigned char buffer[1024] = {};
        ViUInt32 readCount = 0;

        ViStatus st = viRead(
            session,
            buffer,
            static_cast<ViUInt32>(sizeof(buffer) - 1),
            &readCount);

        if (!VisaSucceeded(st))
            return false;

        buffer[readCount] = '\0';
        response.assign(reinterpret_cast<char*>(buffer), readCount);
        response = Trim(response);
        return true;
    }

    bool LooksLikeDG822(const std::string& idn)
    {
        std::string u = UpperAscii(idn);
        return u.find("RIGOL") != std::string::npos &&
               u.find("DG822") != std::string::npos;
    }

    bool QueryResourceIdn(ViSession rm,
                          const std::string& resource,
                          std::string& idn)
    {
        idn.clear();

        ViSession session = VI_NULL;
        ViStatus st = viOpen(
            rm,
            const_cast<ViRsrc>(resource.c_str()),
            VI_NULL,
            2000,
            &session);

        if (!VisaSucceeded(st))
            return false;

        viSetAttribute(session, VI_ATTR_TMO_VALUE, 2000);
        viSetAttribute(session, VI_ATTR_TERMCHAR, '\n');
        viSetAttribute(session, VI_ATTR_TERMCHAR_EN, VI_TRUE);

        bool ok = RawQuery(session, "*IDN?", idn);

        viClose(session);
        return ok;
    }

    std::vector<std::wstring> FindCompatibleUsbResources()
    {
        std::vector<std::wstring> resources;

        ViSession rm = VI_NULL;
        if (!VisaSucceeded(viOpenDefaultRM(&rm)))
            return resources;

        ViFindList findList = VI_NULL;
        ViUInt32 matchCount = 0;
        ViChar descriptor[VI_FIND_BUFLEN] = {};

        // RIGOL's own programming example searches USB VISA resources.
        ViStatus st = viFindRsrc(
            rm,
            const_cast<ViString>("USB?*INSTR"),
            &findList,
            &matchCount,
            descriptor);

        if (!VisaSucceeded(st))
        {
            viClose(rm);
            return resources;
        }

        for (ViUInt32 i = 0; i < matchCount; ++i)
        {
            if (i != 0)
            {
                st = viFindNext(findList, descriptor);
                if (!VisaSucceeded(st))
                    break;
            }

            std::string resource(descriptor);
            std::string idn;

            if (QueryResourceIdn(rm, resource, idn) &&
                LooksLikeDG822(idn))
            {
                resources.push_back(WidenAscii(resource));
            }
        }

        viClose(findList);
        viClose(rm);
        return resources;
    }
}

// -------------------------------------------------------------------------------------------------
// RigolDG822
// -------------------------------------------------------------------------------------------------

RigolDG822::RigolDG822(const wchar_t* resourceId)
    : m_resourceId(resourceId ? resourceId : L""),
      m_defaultRM(VI_NULL),
      m_session(VI_NULL),
      m_channel(1),
      m_highZ(true),
      m_timeoutMs(2000),
      m_ready(false),
      m_outputOn(false)
{
}

RigolDG822::~RigolDG822()
{
    Terminate();
}

bool RigolDG822::ParseInitString(const wchar_t* initString)
{
    // Defaults:
    // CH=1
    // LOAD=HIGHZ
    // TIMEOUT=2000
    if (!initString || initString[0] == L'\0')
        return true;

    std::wstring input(initString);

    // Accept either ';' or ',' separators.
    std::replace(input.begin(), input.end(), L',', L';');

    std::wstringstream ss(input);
    std::wstring token;

    while (std::getline(ss, token, L';'))
    {
        token.erase(
            std::remove_if(token.begin(), token.end(),
                [](wchar_t c) { return std::iswspace(c) != 0; }),
            token.end());

        token = UpperWide(token);

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
            if (value == L"HIGHZ" || value == L"HIZ" ||
                value == L"INF" || value == L"INFINITY")
            {
                m_highZ = true;
            }
            else if (value == L"50" || value == L"50OHM" ||
                     value == L"50OHMS")
            {
                m_highZ = false;
            }
            else
            {
                return false;
            }
        }
        else if (key == L"TIMEOUT")
        {
            try
            {
                unsigned long v = std::stoul(value);
                if (v < 100 || v > 60000)
                    return false;

                m_timeoutMs = static_cast<uint32_t>(v);
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool RigolDG822::OpenVisa()
{
    if (!VisaSucceeded(viOpenDefaultRM(
            reinterpret_cast<ViPSession>(&m_defaultRM))))
    {
        m_defaultRM = VI_NULL;
        return false;
    }

    std::string resource = NarrowAscii(m_resourceId);
    if (resource.empty())
        return false;

    ViSession session = VI_NULL;

    ViStatus st = viOpen(
        static_cast<ViSession>(m_defaultRM),
        const_cast<ViRsrc>(resource.c_str()),
        VI_NULL,
        m_timeoutMs,
        &session);

    if (!VisaSucceeded(st))
        return false;

    m_session = static_cast<unsigned long>(session);

    viSetAttribute(static_cast<ViSession>(m_session),
                   VI_ATTR_TMO_VALUE,
                   m_timeoutMs);

    viSetAttribute(static_cast<ViSession>(m_session),
                   VI_ATTR_TERMCHAR,
                   '\n');

    viSetAttribute(static_cast<ViSession>(m_session),
                   VI_ATTR_TERMCHAR_EN,
                   VI_TRUE);

    return true;
}

void RigolDG822::CloseVisa()
{
    if (m_session != VI_NULL)
    {
        viClose(static_cast<ViSession>(m_session));
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

bool RigolDG822::Query(const std::string& command,
                       std::string& response)
{
    return RawQuery(static_cast<ViSession>(m_session), command, response);
}

bool RigolDG822::IsCompatibleIdn(const std::string& idn) const
{
    return LooksLikeDG822(idn);
}

bool RigolDG822::Initialize(const wchar_t* initString)
{
    Terminate();

    m_channel = 1;
    m_highZ = true;
    m_timeoutMs = 2000;

    if (!ParseInitString(initString))
        return false;

    // If FRA4PicoScope supplied no VISA resource ID, automatically use
    // the first USB-connected RIGOL DG822 that answers *IDN?.
    if (m_resourceId.empty())
    {
        std::vector<std::wstring> found = FindCompatibleUsbResources();
        if (found.empty())
            return false;

        m_resourceId = found.front();
    }

    if (!OpenVisa())
    {
        CloseVisa();
        return false;
    }

    if (!Query("*IDN?", m_idn) || !IsCompatibleIdn(m_idn))
    {
        CloseVisa();
        return false;
    }

    // Start safely with the selected output disabled.
    {
        std::ostringstream cmd;
        cmd << ":OUTP" << m_channel << ":STAT OFF";
        if (!Write(cmd.str()))
        {
            CloseVisa();
            return false;
        }
    }

    // Set the displayed/programmed amplitude unit to Vpp.
    {
        std::ostringstream cmd;
        cmd << ":SOUR" << m_channel << ":VOLT:UNIT VPP";
        if (!Write(cmd.str()))
        {
            CloseVisa();
            return false;
        }
    }

    // Default is HIGHZ because FRA measurements commonly feed a
    // high-impedance scope/DUT. Use LOAD=50 in the init string when appropriate.
    {
        std::ostringstream cmd;
        cmd << ":OUTP" << m_channel << ":LOAD "
            << (m_highZ ? "INF" : "50");

        if (!Write(cmd.str()))
        {
            CloseVisa();
            return false;
        }
    }

    m_outputOn = false;
    m_ready = true;
    return true;
}

bool RigolDG822::Terminate(void)
{
    bool ok = true;

    if (m_session != VI_NULL)
    {
        // Always leave the generator output in a safe OFF state.
        {
            std::ostringstream cmd;
            cmd << ":OUTP" << m_channel << ":STAT OFF";
            if (!Write(cmd.str()))
                ok = false;
        }

        // Return the DG822 Pro front panel to local/manual use.
        // These are the same cleanup commands that were verified
        // in the standalone C++ VISA test.
        if (!Write(":SYST:KLOC OFF"))
            ok = false;

        if (!Write(":SYST:TOUC ON"))
            ok = false;

        // Give the instrument a moment to process the final commands
        // before the USB VISA session is destroyed.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    m_outputOn = false;
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

bool RigolDG822::SetSignalGenerator(double amplitudeVpp,
                                    double offsetV,
                                    double frequencyHz)
{
    if (!Ready())
        return false;

    if (!std::isfinite(amplitudeVpp) ||
        !std::isfinite(offsetV) ||
        !std::isfinite(frequencyHz))
    {
        return false;
    }

    if (frequencyHz < DG822_MIN_SINE_HZ ||
        frequencyHz > DG822_MAX_SINE_HZ)
    {
        return false;
    }

    if (amplitudeVpp < MinAmplitudeVpp() ||
        amplitudeVpp > MaxAmplitudeVpp())
    {
        return false;
    }

    // RIGOL specifies:
    // 2*abs(offset) + amplitude <= amplitude upper limit.
    if ((2.0 * std::abs(offsetV) + amplitudeVpp) >
        (MaxAmplitudeVpp() + 1e-12))
    {
        return false;
    }

    // One SCPI command updates waveform type, frequency, amplitude,
    // offset, and phase together.
    std::ostringstream apply;
    apply << std::setprecision(15)
          << ":SOUR" << m_channel
          << ":APPL:SIN "
          << frequencyHz << ","
          << amplitudeVpp << ","
          << offsetV << ",0";

    if (!Write(apply.str()))
        return false;

    if (!m_outputOn)
    {
        std::ostringstream out;
        out << ":OUTP" << m_channel << ":STAT ON";

        if (!Write(out.str()))
            return false;

        m_outputOn = true;
    }

    // Wait until the DG822 Pro has actually executed all
    // frequency/amplitude/output commands before FRA4PicoScope
    // starts the PicoScope acquisition.
    std::string opc;
    if (!Query("*OPC?", opc))
        return false;

    if (Trim(opc) != "1")
        return false;

    return true;
}

bool RigolDG822::DisableSignalGenerator(void)
{
    if (m_session == VI_NULL)
        return false;

    std::ostringstream cmd;
    cmd << ":OUTP" << m_channel << ":STAT OFF";

    if (!Write(cmd.str()))
        return false;

    m_outputOn = false;
    return true;
}

bool RigolDG822::GetNearestFrequency(double frequency,
                                     double* nearestFrequency)
{
    if (!nearestFrequency || !std::isfinite(frequency))
        return false;

    if (frequency < DG822_MIN_SINE_HZ ||
        frequency > DG822_MAX_SINE_HZ)
    {
        return false;
    }

    // DG822 Pro sine-frequency resolution is 1 uHz or 12 digits.
    // Across this model's 25 MHz sine range, 1 uHz rounding is safe
    // for FRA frequency-point generation.
    *nearestFrequency = std::round(frequency * 1.0e6) / 1.0e6;
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

bool RigolDG822::GetMaxAmplitudeVpp(double* maxAmplitudeVpp,
                                    double minFrequency,
                                    double maxFrequency)
{
    if (!maxAmplitudeVpp)
        return false;

    if (!std::isfinite(minFrequency) ||
        !std::isfinite(maxFrequency) ||
        minFrequency < DG822_MIN_SINE_HZ ||
        maxFrequency > DG822_MAX_SINE_HZ ||
        minFrequency > maxFrequency)
    {
        return false;
    }

    *maxAmplitudeVpp = MaxAmplitudeVpp();
    return true;
}

bool RigolDG822::GetMinAmplitudeVpp(double* minAmplitudeVpp,
                                    double minFrequency,
                                    double maxFrequency)
{
    if (!minAmplitudeVpp)
        return false;

    if (!std::isfinite(minFrequency) ||
        !std::isfinite(maxFrequency) ||
        minFrequency < DG822_MIN_SINE_HZ ||
        maxFrequency > DG822_MAX_SINE_HZ ||
        minFrequency > maxFrequency)
    {
        return false;
    }

    *minAmplitudeVpp = MinAmplitudeVpp();
    return true;
}

bool RigolDG822::IsDDS(double* dacFrequency,
                       uint8_t* phaseAccumulatorSize)
{
    // DG800 Pro uses RIGOL SiFi II rather than a conventional DDS model.
    // Returning false tells FRA4PicoScope not to quantize frequencies using
    // a DDS DAC-clock / phase-accumulator calculation.
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

// -------------------------------------------------------------------------------------------------
// DLL exports expected by FRA4PicoScope
// -------------------------------------------------------------------------------------------------

ExtSigGen* OpenExtSigGen(const wchar_t* id)
{
    // The actual VISA connection is opened in Initialize().
    return new RigolDG822(id);
}

void CloseExtSigGen(ExtSigGen* pESG)
{
    if (!pESG)
        return;

    // ExtSigGen's destructor is not virtual, so delete through the
    // concrete type that this factory created.
    RigolDG822* rigol = static_cast<RigolDG822*>(pESG);
    delete rigol;
}

bool EnumerateExtSigGen(uint16_t* count,
                        wchar_t* ids,
                        uint16_t* idLth)
{
    if (!count || !idLth)
        return false;

    std::vector<std::wstring> resources = FindCompatibleUsbResources();

    *count = static_cast<uint16_t>(
        std::min<size_t>(resources.size(), 0xFFFFu));

    std::wstring joined;

    for (size_t i = 0; i < resources.size(); ++i)
    {
        if (i != 0)
            joined += L",";

        joined += resources[i];
    }

    // FRA4PicoScope currently passes sizeof(wchar_t[1024]), therefore
    // idLth is treated as a BYTE count here.
    const size_t requiredBytes =
        (joined.size() + 1) * sizeof(wchar_t);

    if (requiredBytes > 0xFFFFu)
        return false;

    if (!ids || *idLth < requiredBytes)
    {
        *idLth = static_cast<uint16_t>(requiredBytes);
        return false;
    }

    std::copy(joined.begin(), joined.end(), ids);
    ids[joined.size()] = L'\0';

    *idLth = static_cast<uint16_t>(requiredBytes);
    return true;
}
