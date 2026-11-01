// msimg32.dll entry points, forwarded by the loader to the System32 copy.
// Forwarders keep every register, so the ordinal-only DllInitialize stays safe.

#pragma comment(linker, "/export:vSetDdrawflag=\\.\GLOBALROOT\SystemRoot\System32\msimg32.vSetDdrawflag,@1")
#pragma comment(linker, "/export:AlphaBlend=\\.\GLOBALROOT\SystemRoot\System32\msimg32.AlphaBlend,@2")
#pragma comment(linker, "/export:DllInitialize=\\.\GLOBALROOT\SystemRoot\System32\msimg32.DllInitialize,@3")
#pragma comment(linker, "/export:GradientFill=\\.\GLOBALROOT\SystemRoot\System32\msimg32.GradientFill,@4")
#pragma comment(linker, "/export:TransparentBlt=\\.\GLOBALROOT\SystemRoot\System32\msimg32.TransparentBlt,@5")
