#pragma once

// RIGOL DG822 Pro driver for the FRA4PicoScope external signal generator
// interface. VISA types stay out of this header so that the plugin and
// discovery code only depend on the ExtSigGen interface.

#include "../Plugin/ExtSigGen.h"

#include <cstdint>
#include <string>
#include <vector>

class RigolDG822 final : public ExtSigGen
{
public:
    static constexpr uint32_t kDefaultTimeoutMs = 10000;

    // resourceId: VISA resource name. Empty = use the first DG822 found on USB.
    explicit RigolDG822(const wchar_t* resourceId);

    // ExtSigGen's destructor is not virtual: always delete through RigolDG822*.
    ~RigolDG822();

    RigolDG822(const RigolDG822&) = delete;
    RigolDG822& operator=(const RigolDG822&) = delete;

    bool Initialize(const wchar_t* initString) override;
    bool Terminate(void) override;
    bool Ready(void) override;

    bool SetSignalGenerator(double amplitudeVpp, double offsetV, double frequencyHz) override;
    bool DisableSignalGenerator(void) override;

    bool GetNearestFrequency(double frequency, double* nearestFrequency) override;
    bool GetMaxFrequency(double* maxFrequency) override;
    bool GetMinFrequency(double* minFrequency) override;
    bool GetMaxAmplitudeVpp(double* maxAmplitudeVpp, double minFrequency, double maxFrequency) override;
    bool GetMinAmplitudeVpp(double* minAmplitudeVpp, double minFrequency, double maxFrequency) override;

    bool IsDDS(double* dacFrequency, uint8_t* phaseAccumulatorSize) override;
    bool SupportsDcOffset(void) override;

    // ---- Helpers shared with Detection/InstrumentDiscovery.cpp ----

    // Opens resource briefly and reads *IDN?.
    static bool QueryIdn(const std::string& resource, uint32_t timeoutMs, std::string& idn);

    // True for "RIGOL ...,DG822[ Pro],..." replies.
    static bool IsCompatibleIdn(const std::string& idn);

private:
    enum class Completion { Ok, InstrumentError, CommError };

    bool ParseInitString(const wchar_t* initString);

    bool OpenVisa();
    void CloseVisa();

    bool Write(const std::string& command);
    bool Query(const std::string& command, std::string& response);

    // *OPC? followed by draining :SYST:ERR?.
    Completion CheckCompleted();

    // Write + CheckCompleted. A SCPI error switches the output off and keeps the
    // session; a communication error closes it (Initialize() is needed again).
    bool Execute(const std::string& command);

    // ":OUTP" -> ":OUTP1" for the selected channel.
    std::string Ch(const char* prefix) const;

    double MaxAmplitudeVpp() const;
    double MinAmplitudeVpp() const;

private:
    std::wstring m_resourceId;
    std::string  m_idn;

    unsigned long m_defaultRM = 0; // ViSession
    unsigned long m_session = 0;   // ViSession

    int      m_channel = 1;
    bool     m_highZ = true;
    uint32_t m_timeoutMs = kDefaultTimeoutMs;
    bool     m_ready = false;
};

// Implemented in Detection/InstrumentDiscovery.cpp.
// VISA resource names of every USB-connected DG822 that answers *IDN?.
std::vector<std::wstring> FindDG822Resources();
