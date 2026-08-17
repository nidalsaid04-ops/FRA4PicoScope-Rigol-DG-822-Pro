#pragma once

#include <cstdint>
#include <string>

// ExtSigGen.h is the interface supplied by FRA4PicoScope.
// The Visual Studio project adds ..\FRA4PicoScope to the include path.
#include "ExtSigGen.h"

class RigolDG822 : public ExtSigGen
{
public:
    explicit RigolDG822(const wchar_t* resourceId);
    ~RigolDG822();

    bool Initialize(const wchar_t* initString) override;
    bool Terminate(void) override;
    bool Ready(void) override;

    bool SetSignalGenerator(double amplitudeVpp,
                            double offsetV,
                            double frequencyHz) override;

    bool DisableSignalGenerator(void) override;

    bool GetNearestFrequency(double frequency,
                             double* nearestFrequency) override;

    bool GetMaxFrequency(double* maxFrequency) override;
    bool GetMinFrequency(double* minFrequency) override;

    bool GetMaxAmplitudeVpp(double* maxAmplitudeVpp,
                            double minFrequency,
                            double maxFrequency) override;

    bool GetMinAmplitudeVpp(double* minAmplitudeVpp,
                            double minFrequency,
                            double maxFrequency) override;

    bool IsDDS(double* dacFrequency,
               uint8_t* phaseAccumulatorSize) override;

    bool SupportsDcOffset(void) override;

private:
    bool OpenVisa();
    void CloseVisa();

    bool Write(const std::string& command);
    bool Query(const std::string& command, std::string& response);

    bool ParseInitString(const wchar_t* initString);
    bool IsCompatibleIdn(const std::string& idn) const;

    double MaxAmplitudeVpp() const;
    double MinAmplitudeVpp() const;

private:
    std::wstring m_resourceId;
    std::string  m_idn;

    unsigned long m_defaultRM;
    unsigned long m_session;

    int      m_channel;
    bool     m_highZ;
    uint32_t m_timeoutMs;

    bool m_ready;
    bool m_outputOn;
};

extern "C"
{
    __declspec(dllexport) ExtSigGen* OpenExtSigGen(const wchar_t* id);
    __declspec(dllexport) void CloseExtSigGen(ExtSigGen* pESG);

    __declspec(dllexport) bool EnumerateExtSigGen(
        uint16_t* count,
        wchar_t* ids,
        uint16_t* idLth);
}
