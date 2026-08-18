# FRA4PicoScope – Rigol DG822 Pro Plugin

Adding an external signal generator to the **FRA4PicoScope** software, this project supports the **Rigol DG822 Pro** arbitrary waveform/function generator via VISA.
This project aims to enable the FRA4PicoScope software to control the Rigol DG822 Pro as an external signal source during frequency response measurements.

> **Project status:** Development / testing\
> This plugin is currently intended for experimental use and may still require additional testing across different FRA4PicoScope and VISA configurations.

---

## Overview

FRA4PicoScope can perform frequency-response analysis using PicoScope hardware.\
For measurements requiring an external signal generator, FRA4PicoScope provides an external signal-generator interface through `ExtSigGen.h`.

This project implements that interface for the **Rigol DG822 Pro**.

The plugin communicates with the generator using the **VISA API** and SCPI commands.

### Basic architecture

```text
FRA4PicoScope
      │
      │ ExtSigGen interface
      ▼
RigolDG822.dll
      │
      │ VISA / SCPI
      ▼
Rigol DG822 Pro
```

---

## Features

Current implementation includes support for:

- Rigol DG822 Pro external signal generator control
- VISA-based instrument communication
- SCPI command control
- Frequency configuration
- Output amplitude control
- Generator output control
- Integration with the FRA4PicoScope `ExtSigGen` interface
- 32-bit Visual Studio build configuration

Additional features and compatibility testing may be added as development continues.

---

## Supported Hardware

### Signal generator

Currently developed for:

- **Rigol DG822 Pro**

Other Rigol DG800-series / DG800 Pro-series generators may use similar SCPI commands, but they are **not guaranteed to work unless tested**.

If you successfully test another model, please open an issue or submit a pull request.

---

## Requirements

### Software

- Windows 10 or Windows 11
- Visual Studio 2022 with **Desktop development with C++**
- FRA4PicoScope
- VISA implementation providing:
  - `visa.h`
  - `visa32.lib`

### Hardware

- PicoScope supported by FRA4PicoScope
- Rigol DG822 Pro

---

## Repository Structure

```text
FRA4PicoScope-Rigol-DG822-Pro/
│
│── Examples/
│   └── visa_idn_test.py
│
├── include/
│   └── ExtSigGen.h
│
├── RigolDG822/
│   ├── RigolDG822.cpp
│   ├── RigolDG822.h
│   ├── RigolDG822.def
│   ├── RigolDG822.vcxproj
│   └── RigolDG822.vcxproj.filters
│
└── README.md
```

### Main files

| File                 | Purpose                                                 |
| -------------------- | ------------------------------------------------------- |
| `RigolDG822.cpp`     | Main plugin implementation and instrument communication |
| `RigolDG822.h`       | Plugin declarations                                     |
| `RigolDG822.def`     | DLL export definitions                                  |
| `RigolDG822.vcxproj` | Visual Studio C++ project                               |
| `ExtSigGen.h`        | FRA4PicoScope external signal-generator interface       |

---

# Building

## 1. Install VISA

Install a VISA implementation compatible with the Rigol DG822 Pro.

The build requires access to:

```text
visa.h
visa32.lib
```

A typical VISA installation on Windows may be located under:

```text
C:\Program Files (x86)\IVI Foundation\VISA\WinNT
```

The exact location depends on your VISA installation.

---

## 2. Configure `VISA_ROOT`

The Visual Studio project uses the `VISA_ROOT` environment variable instead of hard-coded paths.

For example:

```text
VISA_ROOT=C:\Program Files (x86)\IVI Foundation\VISA\WinNT
```

You can create it in PowerShell with:

```powershell
[Environment]::SetEnvironmentVariable(
    "VISA_ROOT",
    "C:\Program Files (x86)\IVI Foundation\VISA\WinNT",
    "User"
)
```

Restart Visual Studio after creating or changing this variable.

The project can then reference:

```text
$(VISA_ROOT)\Include
```

and, for a 32-bit build:

```text
$(VISA_ROOT)\Lib_x32\msc
```

---

## 3. Clone the Repository

```bash
git clone https://github.com/nidalsaid04-ops/FRA4PicoScope-Rigol-DG-822-Pro.git
cd FRA4PicoScope-Rigol-DG-822-Pro
```

> The repository may remain private during development.

---

## 4. Open the Visual Studio Project

Open:

```text
RigolDG822/RigolDG822.vcxproj
```

in Visual Studio 2022.

Select:

```text
Configuration: Release
Platform: x32
```

Then choose:

```text
Build → Build Solution
```

A successful build should produce:

```text
RigolDG822.dll
```

along with the normal Visual Studio build files.

---

# Installing the Plugin

After building the project, copy:

```text
RigolDG822.dll
```

to the location where your FRA4PicoScope installation loads external signal-generator plugins.
Restart FRA4PicoScope after installing the DLL.

> The exact plugin installation path may depend on your FRA4PicoScope build or installation method.

---

# Connecting the Rigol DG822 Pro

The plugin communicates with the generator through VISA.

Before running FRA4PicoScope:

1. Connect the Rigol DG822 Pro to the computer.
2. Turn the generator on.
3. Verify that the computer detects the instrument.
4. Verify that your VISA software can communicate with the generator.
5. Start FRA4PicoScope.
6. Select/configure the Rigol external signal generator.

Depending on your VISA configuration, communication may be possible through interfaces such as USB or LAN.

# PyVISA Instrument Detection Test

A small Python script that detects VISA-compatible instruments and sends the standard SCPI `*IDN?` query.

Useful for testing communication with instruments such as Rigol function generators, oscilloscopes, power supplies, and other VISA/SCPI devices.

## Requirements

- Python 3
- PyVISA

Install PyVISA:

```bash
pip install pyvisa
```

## Usage

Connect your instrument by USB, LAN, or another supported VISA interface, then run:

```bash
python visa_idn_test.py
```

Example output:

```text
VISA backend: ...
Resources found: ('USB0::...::INSTR',)

Trying: USB0::...::INSTR
SUCCESS
IDN: RIGOL TECHNOLOGIES,...
```

The script:

- Lists all detected VISA resources
- Opens each resource
- Sends `*IDN?`
- Prints the instrument identification response
- Reports communication errors if a device cannot be queried

# Development Notes

This project was developed as an extension to FRA4PicoScope rather than as a modification of the complete FRA4PicoScope source tree.

Keeping the plugin in a separate repository has several advantages:

- Smaller repository
- Cleaner Git history
- Easier maintenance
- Easier testing of plugin changes
- No need to redistribute the complete FRA4PicoScope source
- Clear separation between upstream FRA4PicoScope and Rigol-specific code

---

# Troubleshooting

## `visa.h` cannot be found

Example error:

```text
Cannot open include file: 'visa.h'
```

Check that:

```text
VISA_ROOT
```

is correctly defined and that this directory exists:

```text
%VISA_ROOT%\Include
```

It should contain:

```text
visa.h
```

---

## `visa32.lib` cannot be found

Check that the Visual Studio library path points to the correct VISA library directory.

For a typical 64-bit installation:

```text
$(VISA_ROOT)\Lib_x64\msc
```

Also verify that:

```text
visa32.lib
```

exists in that directory.

---

## DLL builds but FRA4PicoScope does not detect it

Check:

- DLL architecture matches FRA4PicoScope architecture
- Required DLL exports are present
- VISA runtime is installed
- Rigol generator is visible to VISA
- Plugin DLL is located in the correct FRA4PicoScope directory

---

## Generator is detected but does not respond

Verify communication independently using your VISA software.

The generator should respond to a standard SCPI identification query such as:

```text
*IDN?
```

A communication failure at this stage normally indicates a VISA, connection, or instrument-configuration problem rather than FRA4PicoScope itself.

---

# Roadmap

Possible future improvements include:

- [ ] Automatic VISA resource discovery
- [ ] Improved connection/error reporting
- [ ] Additional Rigol DG800-series compatibility testing
- [ ] Support for additional communication interfaces
- [ ] More robust SCPI error handling
- [ ] Automated build workflow
- [ ] Precompiled DLL releases
- [ ] Screenshots and setup examples
- [ ] FRA measurement examples using the DG822 Pro

---

# Contributing

Testing, bug reports, and improvements are welcome.

Useful information in a bug report includes:

- FRA4PicoScope version
- Rigol generator model
- VISA implementation/version
- Connection type
- Windows version
- Error message or log output
- Steps required to reproduce the problem

---

# Credits

This project integrates with **FRA4PicoScope** via its external signal generator interface.
The `ExtSigGen.h` file is part of the FRA4PicoScope project and must retain all original copyright and licensing information.
We thank the FRA4PicoScope project and its contributors for providing the external signal generator interface that enables the development of this plugin.
Rigol, PicoScope, and other product names are trademarks of their respective owners.
This is an independent community project and is not affiliated with or endorsed by Rigol Technologies or Pico Technology.

---

## License

This project is licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**, in accordance with the licensing terms of FRA4PicoScope.
This project contains code and interfaces derived from FRA4PicoScope. Original copyright and attribution notices are retained in the relevant source files.

---

## Author

**Nidal Said**

Electronics and software project focused on adding Rigol DG822 Pro support to FRA4PicoScope.
