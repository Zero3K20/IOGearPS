//variable
tabindex = 0;
textindex = 0;

var iIndex = 0;

tabArray=['IOGEAR GPSU21','USB Port Print Server','System','Printer','TCP/IP','Services','NetWare','AppleTalk','SNMP','SMB',''];
//Language : English

//system.htm
headArray[iIndex++] = "<BR>This page displays the general system information of the print server.<BR>";
//printer.htm
headArray[iIndex++] = "<BR>This page displays the information of the printer which is currently connected to the print server.<BR>Note: If your printer does not support bi-directional function, some information may not be correctly displayed.";
//tcpip.htm
headArray[iIndex++] = "<BR>This page displays the current TCP/IP settings of the print server.<BR>";
//services.htm
headArray[iIndex++] = "<BR>This page displays the service information of the print server.<BR>";
//netware.htm
headArray[iIndex++] = "<BR>This page displays the current NetWare settings of the print server. <BR>";
//apple.htm
headArray[iIndex++] = "<BR>This page displays the current AppleTalk settings of the print server.<BR>";
//snmp.htm
headArray[iIndex++] = "<BR>This page displays the current SNMP settings of the print server.<BR>";
//smb.htm
headArray[iIndex++] = "<BR>This page displays the printer sharing settings for Microsoft Windows networks.<BR>";
iIndex = 0;



//system.htm
textArray0[iIndex++]="System Information";
textArray0[iIndex++]="Print Server Name :";
textArray0[iIndex++]="System Contact :";
textArray0[iIndex++]="System Location :";
textArray0[iIndex++]="System Up Time :";
textArray0[iIndex++]="Firmware Version :";
textArray0[iIndex++]="MAC Address :";
textArray0[iIndex++]="E-mail Alert :";
textArray0[iIndex++]="Disabled";
textArray0[iIndex++]="Enabled";
//printjob.htm
textArray0[iIndex++]="Print Jobs";
textArray0[iIndex++]="Job";
textArray0[iIndex++]="User";
textArray0[iIndex++]="Elapsed Time";
textArray0[iIndex++]="Protocol";
textArray0[iIndex++]="Port";
textArray0[iIndex++]="Status";
textArray0[iIndex++]="Bytes Printed";
textArray0[iIndex++]="View Job Log";
iIndex = 0;

//printer.htm
textArray1[iIndex++]="Printer Information";
textArray1[iIndex++]="Manufacturer :";
textArray1[iIndex++]="Model Number :";
textArray1[iIndex++]="Printing Language Supported :";
textArray1[iIndex++]="Current Status :";
textArray1[iIndex++]="Waiting for job";
textArray1[iIndex++]="Paper Out";
textArray1[iIndex++]="Offline";
textArray1[iIndex++]="Printing";
textArray1[iIndex++]="Print Speed :";
textArray1[iIndex++]="Fast";
textArray1[iIndex++]="Medium";
textArray1[iIndex++]="Slow";
iIndex = 0;

//netware.htm
textArray2[iIndex++]="General Settings";
textArray2[iIndex++]="Print Server Name :";
textArray2[iIndex++]="Polling Time :";
textArray2[iIndex++]="seconds";
textArray2[iIndex++]="NetWare NDS Settings";
textArray2[iIndex++]="Use NDS Mode :";
textArray2[iIndex++]="Disabled";
textArray2[iIndex++]="Enabled";
textArray2[iIndex++]="Name of the NDS Tree :";
textArray2[iIndex++]="Name of the NDS Context :";
textArray2[iIndex++]="Current Status:";
textArray2[iIndex++]="Disconnected";
textArray2[iIndex++]="Connected";
textArray2[iIndex++]="NetWare Bindery Settings";
textArray2[iIndex++]="Use Bindery Mode :";
textArray2[iIndex++]="Disabled";
textArray2[iIndex++]="Enabled";
textArray2[iIndex++]="Name of the File Server :";
textArray2[iIndex++]="Current Status :";
textArray2[iIndex++]="Disconnected";
textArray2[iIndex++]="Connected";
iIndex = 0;
//tcpip.htm
textArray3[iIndex++]="TCP/IP Settings";
textArray3[iIndex++]="Use DHCP/BOOTP :";
textArray3[iIndex++]="Disabled (Fixed IP address)";
textArray3[iIndex++]="Enabled (Obtain an IP address automatically)";
textArray3[iIndex++]="IP Address :";
textArray3[iIndex++]="Subnet Mask :";
textArray3[iIndex++]="Gateway :";
//randvoo.htm
textArray3[iIndex++]="Rendezvous (Bonjour) Settings";
textArray3[iIndex++]="Status :";
//textArray3[iIndex++]="Disabled";
//textArray3[iIndex++]="Enabled";
textArray3[iIndex++]="Service Name :";
iIndex = 0;
//apple.htm
textArray4[iIndex++]="AppleTalk Settings";
textArray4[iIndex++]="AppleTalk Zone Name :";
textArray4[iIndex++]="Printer Information";
textArray4[iIndex++]="Port Name :";
textArray4[iIndex++]="Printer Type :";
textArray4[iIndex++]="Data Format :";
iIndex = 0;
//snmp.htm
textArray5[iIndex++]="SNMP Community Settings";
textArray5[iIndex++]="SNMP Community 1 :";
textArray5[iIndex++]="Read-Only";
textArray5[iIndex++]="Read-Write";
textArray5[iIndex++]="SNMP Community 2 :";
textArray5[iIndex++]="Read-Only";
textArray5[iIndex++]="Read-Write";
textArray5[iIndex++]="SNMP Trap Settings";
textArray5[iIndex++]="Send SNMP Traps :";
textArray5[iIndex++]="Disabled";
textArray5[iIndex++]="Enabled";
textArray5[iIndex++]="Use Authentication Traps :";
textArray5[iIndex++]="Disabled";
textArray5[iIndex++]="Enabled";
textArray5[iIndex++]="Trap Address 1 :";
textArray5[iIndex++]="Trap Address 2 :";
iIndex = 0;

//joblog.htm
// Translate                                  only "Refresh " is to be translated
textArray6[iIndex++]='<input type=button value=" Refresh " onClick="window.location.reload()">';
textArray6[iIndex++]="Print Jobs";
textArray6[iIndex++]="Job";
textArray6[iIndex++]="User";
textArray6[iIndex++]="Elapsed Time";
textArray6[iIndex++]="Protocol";
textArray6[iIndex++]="Port";
textArray6[iIndex++]="Status";
textArray6[iIndex++]="Bytes Printed";
// Translate                                  only "Close " is to be translated
textArray6[iIndex++]='<input type=button value=" Close " onClick="window.close()">';
iIndex = 0;

//smb.htm
textArray7[iIndex++]="Workgroup";
textArray7[iIndex++]="Name :";
textArray7[iIndex++]="Shared Printer Name";
textArray7[iIndex++]="Printer :";
iIndex = 0;

//services.htm
textArray8[iIndex++]="Printing Method";
textArray8[iIndex++]="Use NetWare Bindery :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Use NetWare NDS :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Use LPR/LPD :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Use AppleTalk :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Use IPP :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Use SMB :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Services";
textArray8[iIndex++]="Telnet :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="SNMP :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="E-mail and EOF Alert :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="HTTP :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";

// Title or Model Name
function TitleModelName()
{
	document.write('<title>GPSU21 USB Print Server</title>');
}

// MM_preloadImages
function BodyPreloadImages()
{
	document.write("<body onload=MM_preloadImages('images/menubtn-cs-setup2.jpg','images/menubtn-cs-misc2.jpg','images/menubtn-cs-restart2.jpg')>");
}

// mainView-Title
function MainViewTitle()
{
	document.write('<img src="images/left.gif"><img src="images/right.gif">');
}

// Row MenuBtn
function RowMenuBtn()
{
	document.write('<td><div class="MenuBtnStatusSelected"');
	document.write(' style="position:relative;"><div>Status</div></div></td>');

	document.write('<td><div class="MenuBtnSetup" onClick="location.href=');
	document.write("'csystem.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Setup</div></div></td>');

	document.write('<td><div class="MenuBtnMisc" onClick="location.href=');
	document.write("'default.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Misc</div></div></td>');

	document.write('<td><div class="MenuBtnRestart" onClick="location.href=');
	document.write("'reset.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Restart</div></div></td>');
}

// out of English
