# Rigol DG822 Pro Plugin for FRA4PicoScope

Use the Rigol DG822 Pro as an external signal generator for Bode plots in FRA4PicoScope. During a sweep, the plugin sets the generator’s frequency, amplitude, and DC offset.


## Install on Windows
1. Close FRA4PicoScope. In [GitHub Releases](https://github.com/nidalsaid04-ops/FRA4PicoScope-Rigol-DG-822-Pro/releases), open **Assets** and choose the ZIP that matches your **FRA4PicoScope application**. A 32-bit copy of FRA4PicoScope needs the x86 plugin even on 64-bit Windows.

   | FRA4PicoScope build | Release asset | Typical installation folder |
   | --- | --- | --- |
   | 32-bit (x86 / Win32) | `RigolDG822-x86.zip` | `C:\Program Files (x86)\FRA4PicoScope\` |
   | 64-bit (x64) | `RigolDG822-x64.zip` | `C:\Program Files\FRA4PicoScope\` |

2. Extract `RigolDG822.dll` from the ZIP and copy it **beside `FRA4PicoScope.exe`**. If you have both versions of FRA4PicoScope installed, put each DLL in its matching folder.
3. Install a **VISA runtime such as NI-VISA** with support for the same architecture. Connect the DG822 Pro by USB and power on the generator and PicoScope.
4. Start FRA4PicoScope and select the Rigol as the external signal generator. Leave the initialization string empty to use channel 1 with a high-impedance load.

Older releases contain a single 32-bit DLL. If your release has no x64 asset, you can [build the 64-bit DLL from source](#build-from-source).

![Gain and phase measured with a Rigol DG822 Pro and a PicoScope 2406B](Examples/screenshot.png)


## Generator settings

The default initialization string is `CH=1;LOAD=HIGHZ;TIMEOUT=4000`. To use channel 2 with a 50 Ω termination, enter `CH=2;LOAD=50`. Omitted settings keep their defaults.

| Option | Values | Default |
| --- | --- | --- |
| `CH` | Generator output `1` or `2` | `1` |
| `LOAD` | `HIGHZ` for a high-impedance input, or `50` for a 50 Ω termination | `HIGHZ` |
| `TIMEOUT` | VISA timeout in milliseconds, from `100` to `60000` | `4000` (4 seconds) |


## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Sweep shows an error or stops responding | In FRA4PicoScope, enter `CH=1;LOAD=HIGHZ;TIMEOUT=4000` in the external generator’s initialization string, then retry the sweep. This setting worked on the tested DG822 Pro setup. |
| Plugin does not appear | Confirm your FRA4PicoScope build supports external generators. Check the DLL folder and verify that the plugin and VISA runtime match the application architecture. Restart FRA4PicoScope. |
| Generator is missing | In your VISA software, select the Rigol USB device and send `*IDN?`. The reply should identify the DG822 Pro. The repository also includes a [Python connection test](Examples/visa_idn_test.py) for use with PyVISA. |
| Front panel stays in remote mode after closing FRA4PicoScope | The plugin unlocks the buttons and re-enables touch during shutdown. On DG822 Pro firmware 00.02.01, press the front-panel key to return to local control if needed. |
| Sampling-rate warning near 20 MHz | On the tested PicoScope 2406B setup at 500 MS/s, **Low noise mode**, **Minimum cycles = 16**, and **Low noise oversampling = 16** worked for sweeps up to 20 MHz. |


## Build from source

Install Visual Studio with **Desktop development with C++**, a Windows SDK, and the VISA development files. Building both architectures requires `visa.h`, `visa32.lib`, and `visa64.lib`.

From the repository root, run the following in PowerShell to build both Release DLLs.

```powershell
.\build.cmd
```

| Build | Output |
| --- | --- |
| 32-bit | `bin\Win32\Release\RigolDG822.dll` |
| 64-bit | `bin\x64\Release\RigolDG822.dll` |

To build just one version in Visual Studio, open [RigolSignalGenerator.sln](RigolSignalGenerator.sln), select **Release** and **x86** or **x64**, then choose **Build Solution**. If VISA is installed in a custom location, set the `VisaRoot` environment variable to the folder containing the VISA headers and libraries before building.


## Credits and license

Created by **Nidal Said**, using the FRA4PicoScope external signal-generator interface. Thanks to the FRA4PicoScope project and its contributors.

Licensed under **GNU GPL v3.0 or later (GPL-3.0-or-later)**. See [LICENSE](LICENSE). Original copyright and attribution notices are retained; `ExtSigGen.h` comes from FRA4PicoScope.

This is an independent community project, not affiliated with or endorsed by Rigol Technologies or Pico Technology. Product names belong to their respective owners.
