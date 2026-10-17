# Building Phoenix V3

Windows requires Python, Git and Visual Studio 2022 with the C++ desktop workload/Windows SDK. Run from the repository root in PowerShell:

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1 -Target native
./build/native/Release/native.exe --build-check
./scripts/build.ps1 -Target web
python -m http.server 8080 --directory build/web
```

Open `http://localhost:8080/`. The launcher accepts `type=server|client|workshop`, `mode=editor|gm`, `code` and `slot`. Connection parameters are hints and cannot grant authority. Browser networking is not implemented yet.

Bootstrap installs CMake 3.31.6, Ninja 1.11.1.3 and Emscripten 6.0.12 into ignored `.tools/`; it does not alter the system installation. Initial configuration downloads dependency source archives. CMake pins each by commit and SHA-256: Flecs 4.1.5, raylib 6.0, RmlUi 6.1, ImGui 1.92.0 docking and FreeType 2.13.3. `CMakeLists.txt` contains the immutable revisions and hashes. Fonts retain their upstream licence in the build assets.

Native links the authoritative simulation and tools. Browser server links simulation and presentation; browser client links presentation; browser Workshop links tools and presentation. The libraries are static. A successful build does not establish the pending accessibility, device, gameplay, networking or performance acceptance checks in the implementation plan.
