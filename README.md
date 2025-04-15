# saints-row-4-patches
Saints Row 4 Patches and Additional Files for Building the Source Code

## How to set the patches?
Drag Microsoft GDK folder into **C:\Program Files (x86)**<br>
And last thing to do is put the sr35 file to your Saints Row 4 Source Code Location<br>
Make a new Environment Variable called **NINTENDO_SDK_ROOT** You can point this to an empty folder.<br>
Also download the real game from steam and put all the folders to the sr35/ng2/sr35 directory, do not click replace the files just skip it.
Remove these 2 files(polarssl.c and polarssl.h) inside this folder: ``D:\SR4\sr35\ng2\sr35\src\vendor\Agora Games (Hydra SDK)\external\curl-7.21.4\lib``


# Extras
[Visual Studio 2022](https://visualstudio.microsoft.com/tr/vs/)
- with Desktop C++ Development and C++ Game Development selected as well as Visual C++ MFC for x86 and x64.

[Vulkan SDK](https://sdk.lunarg.com/sdk/download/1.3.296.0/windows/VulkanSDK-1.3.296.0-Installer.exe)<br>
[Volition .reg file](/volition.reg)

Here's the source: https://gofile.io/d/PqdaAk<br>
Password: ``LDFsvm36*q2236.cx-=312"5xc32-1236`12b_sx.[232#6-12cvw0s-``<br>
You can just extract every folder except the data folder(which is 133GB) inside **SR4.7z\main\sr35\ng2\sr35** if you not gonna mess with assets.<br>

### Building Assets
1. Install [Perforce Visual Client and Server](https://mega.nz/file/HyYx3BzR#X_i0lWE1l_Lx-4wZSuwLo9X-Ec_L69OCZ2Im7Txz3w8)
and [Python](https://www.python.org/downloads/)
2. Run **py pip install p4python**

After you install the client and server you will need to run these 2 commands in CMD:<br>
`p4 set P4PORT=(server ip):1666` Set server ip to what you set in P4V it will most likely be localhost:1666<br>
`p4 set P4CLIENT=(WORKSPACENAME)` Set the workspace name to what you have in Perforce.

- You need to gen the pcvdir You can do this running `py dsfl.py genvdir pc` in the `D:\projects\sr35\ng2\sr35` folder.
- **If you don't have perforce setup right dsfl will give errors.**

- You can build packfiles by running `py dsfl.py pack pc full` in the `D:\projects\sr35\ng2\sr35` folder.
- **If you don't have perforce setup right dsfl will give errors.**
