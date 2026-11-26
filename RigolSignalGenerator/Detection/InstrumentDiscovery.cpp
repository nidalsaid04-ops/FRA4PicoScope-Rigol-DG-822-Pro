// Finds RIGOL DG822 generators connected over USB.

#include "../Drivers/RigolDG822.h"

#include <visa.h>

namespace
{
    // Short probe timeout: discovery must stay quick even if a USB
    // instrument is present but does not answer.
    constexpr uint32_t PROBE_TIMEOUT_MS = 2000;

    std::wstring WidenAscii(const std::string& s)
    {
        return std::wstring(s.begin(), s.end());
    }

    std::vector<std::string> FindUsbResources()
    {
        std::vector<std::string> resources;

        ViSession rm = VI_NULL;
        if (viOpenDefaultRM(&rm) < VI_SUCCESS)
            return resources;

        ViFindList findList = VI_NULL;
        ViUInt32 matchCount = 0;
        ViChar descriptor[VI_FIND_BUFLEN] = {};

        // RIGOL's own programming example searches USB VISA resources.
        if (viFindRsrc(rm, const_cast<ViString>("USB?*INSTR"),
                       &findList, &matchCount, descriptor) >= VI_SUCCESS)
        {
            for (ViUInt32 i = 0; i < matchCount; ++i)
            {
                if (i != 0 && viFindNext(findList, descriptor) < VI_SUCCESS)
                    break;
                resources.emplace_back(descriptor);
            }
            viClose(findList);
        }

        viClose(rm);
        return resources;
    }
}

std::vector<std::wstring> FindDG822Resources()
{
    std::vector<std::wstring> found;

    for (const std::string& resource : FindUsbResources())
    {
        std::string idn;
        if (RigolDG822::QueryIdn(resource, PROBE_TIMEOUT_MS, idn) &&
            RigolDG822::IsCompatibleIdn(idn))
        {
            found.push_back(WidenAscii(resource));
        }
    }

    return found;
}
