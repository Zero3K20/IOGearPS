

//variable
tabindex = 0;
textindex = 0;
var iIndex = 0;

//Language : English
tabArray=['PU211','USB Port Print Server','Factory Default','Firmware Upgrade'];

	//upgrade.htm
headArray[iIndex++] ="<br>This page allows you to upgrade the firmware of the print server.<br><br><font color=red>Note:</font> please make sure the firmware is correct before you proceed. If you do not know which firmware file you should use, please contact your local dealer for technical support.";
iIndex = 0;

	//default.htm
textArray1[iIndex++] = "<BR>Click <b>Factory Default</b> then <b>OK</b> to reload all default settings in the print server.<br><br><FONT CLASS=F1 COLOR=#FF3300><B>Warning! All current settings will be erased.</B></FONT>";
textArray1[iIndex++] = "Load default, excluding TCP/IP settings.";
textArray1[iIndex++] = "You can still connect the print server with the current IP address after it restarts.";
textArray1[iIndex++] = "Load default, including TCP/IP settings.";
textArray1[iIndex++] = "You cannot connect the print server with the current IP address after it restarts.<br>Please visit the default IP address 192.168.0.10 instead.";
// Translate                               Only OK is to be translated
textArray1[iIndex++] = '<input type=button  value="&nbsp;&nbsp;OK&nbsp;&nbsp;" onClick="return SaveIPSetting(';
// Begin don't translate
textArray1[iIndex++] = "'DRESTART.HTM');";
textArray1[iIndex++] = '">';
iIndex = 0;
// End don't translate

//upgrade.htm
textArray2[iIndex++]="Firmware Upgrade";
textArray2[iIndex++]="Select Firmware Directory and File:";
// Begin don't translate
textArray2[iIndex++]='<input type=button value="Firmware Upgrade" onClick="return WebUpgrade()">';
iIndex = 0;
// End don't translate

//reset.htm
textArray3[iIndex++]="This page allows you to restart the print server.<br>";
textArray3[iIndex++]="Restart The Print Server<br>";
textArray3[iIndex++]="Do you want to save settings and restart the print server now?<br>";
// Translate                               Only OK is to be translated
textArray3[iIndex++]='<input type=button value="&nbsp;&nbsp;OK&nbsp;&nbsp;" onClick="window.location=';
// Begin don't translate
textArray3[iIndex++]="'RESTART.HTM'";
textArray3[iIndex++]='">';
iIndex = 0;
// End don't translate

	//restart.htm
textArray4[iIndex++] = "Restarting ...";
textArray4[iIndex++] = "Please wait while the print server restarts.";
iIndex = 0;
	//urestart.htm
textArray5[iIndex++] = "Upgrade completed successfully!";
textArray5[iIndex++] = "After upgrading the firmware, the print server will automatically restart, please wait a few moments.";
iIndex = 0;
	//drestart.htm
textArray6[iIndex++] = "Loading Factory Defaults ...";
textArray6[iIndex++] = "After loading the default settings, the print server will automatically restart.<BR><BR>You will be redirected to the Status page when the print server has been restarted.";
iIndex = 0;

// Title or Model Name
function TitleModelName()
{
	document.write('<title>PU211 USB Port Print Server</title>');
}

// MM_preloadImages
function BodyPreloadImages()
{
	document.write("<body onload=MM_preloadImages('imgenus/MenuBtn-E-setup2.jpg','imgenus/MenuBtn-E-misc2.jpg','imgenus/MenuBtn-E-restart2.jpg')>");
}

// mainView-Title
function MainViewTitle()
{
	document.write('<img src="imgenus/mainView-Title-E.jpg" width="576" height="35" />');
}

// Row MenuBtn
function RowMenuBtn()
{
	document.write('<td><div class="MenuBtnStatus" onClick="location.href=');
	document.write("'SYSTEM.HTM'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Status</div></div></td>');

	document.write('<td><div class="MenuBtnSetup" onClick="location.href=');
	document.write("'CSYSTEM.HTM'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Setup</div></div></td>');

	document.write('<td><div class="MenuBtnMiscSelected"');
	document.write(' style="position:relative;"><div>Misc</div></div></td>');

	document.write('<td><div class="MenuBtnRestart" onClick="location.href=');
	document.write("'RESET.HTM'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Restart</div></div></td>');
}

function RowMenuBtn4()
{
	document.write('<td><div class="MenuBtnStatus" onClick="location.href=');
	document.write("'SYSTEM.HTM'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Status</div></div></td>');

	document.write('<td><div class="MenuBtnSetup" onClick="location.href=');
	document.write("'CSYSTEM.HTM'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Setup</div></div></td>');

	document.write('<td><div class="MenuBtnMisc" onClick="location.href=');
	document.write("'DEFAULT.HTM'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Misc</div></div></td>');

	document.write('<td><div class="MenuBtnRestartSelected"');
	document.write(' style="position:relative;"><div>Restart</div></div></td>');
}

// out of English
