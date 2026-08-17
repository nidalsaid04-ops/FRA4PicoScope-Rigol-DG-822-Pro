//////////////////////////////////////////////////////////////////////////////////////////////////
//
// Frequency Response Analyzer for PicoScope
//
// Copyright (c) 2020 by Aaron Hexamer
//
// This file is part of the Frequency Response Analyzer for PicoScope program.
//
// Frequency Response Analyzer for PicoScope is free software: you can 
// redistribute it and/or modify it under the terms of the GNU General Public 
// License as published by the Free Software Foundation, either version 3 of 
// the License, or (at your option) any later version.
//
// Frequency Response Analyzer for PicoScope is distributed in the hope that 
// it will be useful,but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Frequency Response Analyzer for PicoScope.  If not, see <http://www.gnu.org/licenses/>.
//
// Module: ExtSigGen.h: Defines the interface to the DLL plugins for signal generators not
//                      built into a PicoScope.  Includes the class interface for the object to
//                      access and control the signal generator instrument.  All external signal
//                      generator plugins should include this file in their project and define the
//                      macro EXTSIGGENAPI_EXPORTS.
//
//////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef EXTSIGGENAPI_EXPORTS
#define EXTSIGGENAPI_API __declspec(dllexport)
#else
#define EXTSIGGENAPI_API __declspec(dllimport)
#endif

#include <stdint.h>

class ExtSigGen
{
    public:
        EXTSIGGENAPI_API ExtSigGen(void) {};
        EXTSIGGENAPI_API ~ExtSigGen() {};

        EXTSIGGENAPI_API virtual bool Initialize(const wchar_t* _initString) = 0;
        EXTSIGGENAPI_API virtual bool Terminate(void) = 0;
        EXTSIGGENAPI_API virtual bool Ready(void) = 0;
        EXTSIGGENAPI_API virtual bool SetSignalGenerator(double amplitudeVpp, double offsetV, double frequencyHz) = 0;
        EXTSIGGENAPI_API virtual bool DisableSignalGenerator(void) = 0;
        EXTSIGGENAPI_API virtual bool GetNearestFrequency(double frequency, double* nearestFrequency) = 0;
        EXTSIGGENAPI_API virtual bool GetMaxFrequency(double* maxFrequency) = 0;
        EXTSIGGENAPI_API virtual bool GetMinFrequency(double* minFrequency) = 0;
        EXTSIGGENAPI_API virtual bool GetMaxAmplitudeVpp(double* maxAmplitudeVpp, double minFrequency, double maxFrequency) = 0;
        EXTSIGGENAPI_API virtual bool GetMinAmplitudeVpp(double* minAmplitudeVpp, double minFrequency, double maxFrequency) = 0;
        EXTSIGGENAPI_API virtual bool IsDDS(double* dacFrequency, uint8_t* phaseAccumulatorSize) = 0;
        EXTSIGGENAPI_API virtual bool SupportsDcOffset(void) = 0;
};

// Define prototypes for functions that exist in the DLL
// To create and return an external signal generator object, opening the device with id
typedef ExtSigGen* (*fnOpenExtSigGen)(const wchar_t* id);
// To destroy external signal generator, closing the device before the library is unloaded
typedef void (*fnCloseExtSigGen)(ExtSigGen* pESG);
// To enumerate external signal generators
typedef bool (*fnEnumerateExtSigGen)(uint16_t* count, wchar_t* ids, uint16_t* idLth);