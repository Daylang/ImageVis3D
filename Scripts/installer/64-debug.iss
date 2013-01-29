;  For more information, please see: http://software.sci.utah.edu
;
;  The MIT License
;
;  Copyright (c) 2009 Scientific Computing and Imaging Institute,
;  University of Utah.
;
;
;  Permission is hereby granted, free of charge, to any person obtaining a
;  copy of this software and associated documentation files (the "Software"),
;  to deal in the Software without restriction, including without limitation
;  the rights to use, copy, modify, merge, publish, distribute, sublicense,
;  and/or sell copies of the Software, and to permit persons to whom the
;  Software is furnished to do so, subject to the following conditions:
;
;  The above copyright notice and this permission notice shall be included
;  in all copies or substantial portions of the Software.
;
;  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
;  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
;  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
;  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
;  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
;  DEALINGS IN THE SOFTWARE.
;
;
;
;  Hilbert Curve implementation copyright 1998, Rice University.
;
; 
; 
;  LZ4 - Fast LZ compression algorithm
;  Copyright (C) 2011-2012, Yann Collet.
;  BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
;  
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions are
;  met:
;  
;     * Redistributions of source code must retain the above copyright
;  notice, this list of conditions and the following disclaimer.
;     * Redistributions in binary form must reproduce the above
;  copyright notice, this list of conditions and the following disclaimer
;  in the documentation and/or other materials provided with the
;  distribution.
;  
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;
;
;
; ImageVis3D 64 bit installer script: creates an installer for amd64 systems.
; From the root of the ImageVis3D subversion tree, process this file with
; "iscc.exe" from Inno Setup 5.
[Setup]
AppName=ImageVis3D
AppVerName=ImageVis3D 2.2.0
AppVersion=2.2.0
AppPublisher=SCI Institute
AppPublisherURL=http://software.sci.utah.edu/
AppSupportURL=http://software.sci.utah.edu/
AppUpdatesURL=http://software.sci.utah.edu/
AppCopyright=Copyright (c) 2012 Scientific Computing and Imaging Institute, University of Utah.
DefaultDirName={pf}\ImageVis3D
DefaultGroupName=ImageVis3D
OutputDir=Scripts\installer
OutputBaseFilename=ImageVis3D-64bit-debug
AllowNoIcons=no
Compression=lzma/ultra
InternalCompressLevel=ultra
SolidCompression=yes
SourceDir=../../
LicenseFile=LICENSE
; Install on Windows XP or newer (actually a Windows NT build number)
MinVersion=0,5.01
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: english; MessagesFile: compiler:Default.isl

[Tasks]
Name: desktopicon; Description: {cm:CreateDesktopIcon}; GroupDescription: {cm:AdditionalIcons}; Flags: unchecked

[Dirs]
Name: {app}
Name: {userappdata}\ImageVis3D; Flags: uninsalwaysuninstall

[Files]
; Dependencies.
;   MS redistributable crap.
Source: C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\x64\Microsoft.VC100.CRT\*; DestDir: {app}; Flags: recursesubdirs
Source: "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\x64\Microsoft.VC100.MFC\*"; DestDir: "{app}"; Flags: recursesubdirs
;Source: "vcredist_x64.exe"; DestDir: {tmp}; DestName: vcredist.exe; Flags: deleteafterinstall;


; ImageVis3D
Source: build\x64\Debug\ImageVis3D-64.exe; DestDir: {app}; Flags: ignoreversion replacesameversion
Source: Tuvok\Shaders\*; DestDir: {app}\Shaders; Excludes: .svn; Flags: ignoreversion replacesameversion
Source: Scripts\installer\imagevis3d.pdf; DestDir: {app}; Flags: ignoreversion replacesameversion
Source: Scripts\installer\GettingDataIntoImageVis3D.pdf; DestDir: {app}; Flags: ignoreversion replacesameversion

[Icons]
Name: {group}\ImageVis3D; Filename: {app}\ImageVis3D-64.exe; WorkingDir: {app}
Name: {commondesktop}\ImageVis3D; Filename: {app}\ImageVis3D-64.exe; WorkingDir: {app}
Name: {group}\{cm:UninstallProgram,ImageVis3D}; Filename: {uninstallexe}
Name: {group}\Manual; Filename: {app}\ImageVis3D.pdf; WorkingDir: {app}
Name: {group}\Manual; Filename: {app}\GettingDataIntoImageVis3D.pdf; WorkingDir: {app}


[Run]
Filename: {app}\ImageVis3D-64.exe; Description: {cm:LaunchProgram,ImageVis3D}; Flags: nowait postinstall
;Filename: {tmp}\vcredist.exe; StatusMsg: "Installing required Visual C++ runtime..."

[UninstallDelete]
Type: filesandordirs; Name: {userappdata}\ImageVis3D*
