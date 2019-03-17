# Quiver
Custom Source Engine branch based on Source Engine 2007

## Building

### Windows

1. Run createlibprojects.bat and compile lib files in Release

2. Install [Perl](https://www.perl.org).

3. Run src/materialsystem/stdshadersbuildallshaders.bat

4. Run src/createallprojects.bat or src/createbinprojects.bat and build the solution in Release.

5. Run (and edit) game/Make Junctions.bat

You can run the engine with run_mod_hl2.bat or run_mod_episodic.bat

### Linux

Linux support is unavailable at the moment, you can help. See Supporting Non-Windows Platforms.

### macOS
macOS support is unavailable at the moment, you can help. See Supporting Non-Windows Platforms.

### Supporting Non-Windows Platforms
The Source Engine was built heavily Windows-oriented, as it uses DirectX as it's graphics API which is only available on Windows, Xbox, Xbox 360 and the Xbox One. This is the major obstacle in supporting other platforms. To bring support to other platforms; we need an experienced graphics programmer who can work with OpenGL or Vulkan as these graphics APIs are universal, other than that some platform-specific code may be required.