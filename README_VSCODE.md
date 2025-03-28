# VS Code Development Setup

The trick to making it work is to make sure VSCode is using the ucrt64 binaries for the development toolchain,
NOT anything installed natively to Windows.

1. Install C/C++ extensions for VSCode.
2. Install Meson extension for VSCode.
3. In the settings for Meson, set Meson build path to `C:\msys64\ucrt64\bin\meson` and
set the Build folder to `build`.
4. Add your `C:\msys64\ucrt\bin` folder to your Path (via Environment Variables).
5. Open a windows terminal and make sure that `gcc --version` and `g++ --version` and `gdb --version` return
a version string. You can also confirm with `where gcc` that it's using the binary within msys2.
6. Before opening the folder in vscode, create a `.vscode` subdolder and populate it with some files. See the below "Config files" section for some example config files. Tweak the paths to match your own system.
7. Open a cpp file to make sure the extensions activate.
8. You should now have intellisense working (you should be able to "Go to definition" and "find references, etc...").
9. You can now build using the Meson build task instead of msys2 shell if you prefer. For best
debugging experience you probably want to build the `sapf_unoptimized_testable` target which disables optimizations.
10. You can debug via selecting the "Attach (sapf)" configuration (bottom left), manually
running sapf.exe, and then presing F5 and attaching to the sapf.exe process. (Currently haven't figured out
how to get the "launch" version working - it runs but the text is garbled - likely an encoding issue).
    - If it times out waiting to attach, try restarting VSCode and maybe also
    closing any msys2 shells if you have any open (not sure what's going on here)?



#### Config files
Below setup uses clang but it should work with other toolchains
(clang can be installed via msys2's `mingw-w64-ucrt-x86_64-clang` package)
Example `.vscode/c_cpp_properties.json`
```json
{
    "configurations": [
        {
            "name": "ucrt64",
            "includePath": [
                "C:/msys64/ucrt64/include/**",
                "${workspaceFolder}/**"
            ],
            "defines": [
                "_DEBUG",
                "UNICODE",
                "_UNICODE"
            ],
            "compilerPath": "C:/msys64/ucrt64/bin/clang++.exe",
            "windowsSdkVersion": "10.0.22621.0",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-clang-x64",
            "configurationProvider": "mesonbuild.mesonbuild"
        }
    ],
    "version": 4
}
```

Example `.vscode/launch.json`
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "(gdb) Attach",
            "type": "cppdbg",
            "request": "attach",
            "program": "${workspaceRoot}/build/sapf.exe",
            "MIMode": "gdb",
            "miDebuggerPath": "C:\\msys64\\ucrt64\\bin\\gdb.exe",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
                {
                    "description": "Set Disassembly Flavor to Intel",
                    "text": "-gdb-set disassembly-flavor intel",
                    "ignoreFailures": true
                }
            ]
        },
        {
            "name": "(gdb) Launch (chairbender note - not working ATM, use attach instead - everything is garbled)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceRoot}/build/sapf.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceRoot}/build",
            "environment": [
                { "name": "MSYSTEM", "value": "UCRT64" },
                { "name": "MSYS2_PATH_TYPE", "value": "inherit" },
                { "name": "PATH", "value": "C:\\msys64\\ucrt64\\bin;${env:PATH}" }
            ],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "C:\\msys64\\ucrt64\\bin\\gdb.exe",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
                {
                    "description": "Set Disassembly Flavor to Intel",
                    "text": "-gdb-set disassembly-flavor intel",
                    "ignoreFailures": true
                }
            ]
        },
        {
            "name": "(gdb) Debug Test",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceRoot}/build/test_unoptimized.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceRoot}/build",
            "environment": [
                { "name": "MSYSTEM", "value": "UCRT64" },
                { "name": "MSYS2_PATH_TYPE", "value": "inherit" },
                { "name": "PATH", "value": "C:\\msys64\\ucrt64\\bin;${env:PATH}" }
            ],
            "externalConsole": false,
            "MIMode": "gdb",
            "miDebuggerPath": "C:\\msys64\\ucrt64\\bin\\gdb.exe",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
                {
                    "description": "Set Disassembly Flavor to Intel",
                    "text": "-gdb-set disassembly-flavor intel",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "Meson: Build test_unoptimized:executable"
        }
    ]
}
```

Example `.vscode/settings.json`
```json
{
    "C_Cpp.default.compileCommands": "c:\\msys64\\home\\kwhip\\sapf\\build/compile_commands.json",
    "C_Cpp.default.configurationProvider": "mesonbuild.mesonbuild"
}
```

Example `.vscode/tasks.json`
```json
{
	"version": "2.0.0",
	"tasks": [
		{
			"type": "meson",
			"target": "sapf:executable",
			"mode": "build",
			"problemMatcher": [
				"$meson-gcc"
			],
			"group": "build",
			"label": "Meson: Build sapf:executable"
		}
	]
}
```