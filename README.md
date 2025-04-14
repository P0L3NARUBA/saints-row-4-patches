# saints-row-4-patches
Saints Row 4 Patches for Building the Source Code

## Requirements
[Visual Studio 2017](/vs_Community2017.exe)
[Visual Studio 2019](/vs_Community2019.exe)
[Microsoft GDK October 2023 Update 7](https://github.com/microsoft/GDK/archive/refs/tags/October_2023_Update_7.zip)
[Vulkan SDK](https://sdk.lunarg.com/sdk/download/1.3.296.0/windows/VulkanSDK-1.3.296.0-Installer.exe)
[Volition .reg file](/volition.reg)

### For assets (Not Required)
[Perforce Visual Client](https://www.perforce.com/downloads/helix-visual-client-p4v)
[Perforce Server](https://www.perforce.com/products/helix-core/free-version-control) 
[Python](https://www.python.org/downloads/)

Here's the source: https://gofile.io/d/PqdaAk<br>
Password: ``LDFsvm36*q2236.cx-=312"5xc32-1236`12b_sx.[232#6-12cvw0s-``<br>
Also make a new Environment Variable called **NINTENDO_SDK_ROOT** You can point this to an empty folder.

# How to set the patches?
Drag Microsoft GDK to **C:\Program Files (x86)**
And last thing to is put the sr35 file to your saints row 4 source code.

## Building Assets
1. Install Python. After its installed run **py pip install p4python**
2. Install the Vulkan SDK. The default selected is fine.
3. [Install the Perforce Visual Client and server.](https://www.youtube.com/watch?v=2me3R2FFjcI)

After you install the client and server you will need to run these 2 commands in CMD:
`p4 set P4PORT=(server ip):1666` Set server ip to what you set in P4V it will most likely be localhost:1666
`p4 set P4CLIENT=(WORKSPACENAME)` Set the workspace name to what you have in Perforce.

- You need to gen the pcvdir You can do this running `py dsfl.py genvdir pc` in the `D:\projects\sr35\ng2\sr35` folder.
- **If you don't have perforce setup right dsfl will give errors.**

- You can build packfiles by running `py dsfl.py pack pc full` in the `D:\projects\sr35\ng2\sr35` folder.
- **If you don't have perforce setup right dsfl will give errors.**
