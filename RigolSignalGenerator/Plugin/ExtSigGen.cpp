// DLL entry points loaded by FRA4PicoScope (exported through RigolSignalGenerator.def).

#include "../Drivers/RigolDG822.h"

#include <algorithm>
#include <new>

extern "C"
{
    ExtSigGen* OpenExtSigGen(const wchar_t* id);
    void CloseExtSigGen(ExtSigGen* pESG);
    bool EnumerateExtSigGen(uint16_t* count, wchar_t* ids, uint16_t* idLth);
}

ExtSigGen* OpenExtSigGen(const wchar_t* id)
{
    // The VISA connection itself is opened in Initialize().
    // Never let an exception cross the C boundary into FRA4PicoScope.
    return new (std::nothrow) RigolDG822(id);
}

void CloseExtSigGen(ExtSigGen* pESG)
{
    // ExtSigGen's destructor is not virtual, so delete through the concrete
    // type this factory created.
    delete static_cast<RigolDG822*>(pESG);
}

bool EnumerateExtSigGen(uint16_t* count, wchar_t* ids, uint16_t* idLth)
{
    if (!count || !idLth)
        return false;

    std::vector<std::wstring> resources;
    try
    {
        resources = FindDG822Resources();
    }
    catch (...)
    {
        return false;
    }

    *count = static_cast<uint16_t>(std::min<size_t>(resources.size(), 0xFFFFu));

    std::wstring joined;
    for (size_t i = 0; i < resources.size(); ++i)
    {
        if (i != 0)
            joined += L",";
        joined += resources[i];
    }

    // FRA4PicoScope passes sizeof(wchar_t[1024]), so idLth is a BYTE count.
    const size_t requiredBytes = (joined.size() + 1) * sizeof(wchar_t);
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
