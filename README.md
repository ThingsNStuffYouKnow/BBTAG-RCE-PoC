# Summary
This is a proof-of-concept to show how a stack-based buffer overflow vulnerability can be exploited for remote code execution in the Steam version of BlazBlue: Cross Tag Battle (game by Arc System Works/Arc Sys).\
I don't recommend playing the game online without a fix.\
Temporary fix: https://github.com/ThingsNStuffYouKnow/BBTAG-Fix \
Read for further info: https://github.com/ThingsNStuffYouKnow/BBCF-RCE-PoC

# Vulnerability
![function_bbtag](https://user-images.githubusercontent.com/109482766/179823888-22fe22b0-5d46-4232-b522-30d77d60b6c9.png)

Same vulnerability as recently fixed in BlazBlue: Central Fiction, class containing the vulnerable function is called "Udp" in BBTAG.\
BBTAG.exe is compiled without ASLR and stack canaries.

# PoC
The PoC uses the overflow to jump into a ROP chain, which allocates executable memory and stores some shellcode to start calc.exe.\
Since this is only for demonstration, the ROP chain is not optimized and returning to regular execution is not in scope (process will crash after payload execution).\
To test it yourself, make sure to compile as x86 and load the dll into your BBTAG.exe process at runtime.\
You can then call the exported function "RunPoCOnSteamID", which takes the target Steam ID as a C-style wide string in decimal (for reliable timing, call right after character selection).

# External
https://github.com/SteamRE/open-steamworks
