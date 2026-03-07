
//variable
tabindex = 0;
textindex = 0;
var iIndex = 0;

//Language : English
tabArray=['IOGEAR GPSU21','USB Port Print Server','System','TCP/IP','Services','NetWare','AppleTalk','SNMP','SMB','',''];

//csystem
headArray[iIndex++] = "<BR>This setup page allows you to configure general system settings of the print server.<br>";
//ctcpip.htm
headArray[iIndex++] = "<BR>This setup page allows you to configure TCP/IP settings of the print server.";
//cservices.htm
headArray[iIndex++] = "<BR>This setup page allows you to configure the services of the print server.";
//cnetware.htm
headArray[iIndex++] = "<BR>This setup page allows you to configure the NetWare function of the print server.";
//capple.htm
headArray[iIndex++] ="<BR>This setup page allows you to configure AppleTalk settings of the print server.<br>";
//csnmp.htm
headArray[iIndex++] ="<BR>This setup page allows you to configure SNMP settings of the print server.";
//csmb.htm
headArray[iIndex++] ="<BR>This page displays the printer sharing settings for Microsoft Windows networks.";
iIndex = 0;

//csystem.htm
textArray0[iIndex++]="E-mail Alert Settings";
textArray0[iIndex++]="E-mail Alert :";
textArray0[iIndex++]="Disabled";
textArray0[iIndex++]="Enabled";
textArray0[iIndex++]="SMTP Server IP Address:";
textArray0[iIndex++]="Administrator E-mail Address:";
textArray0[iIndex++]="System Settings";
textArray0[iIndex++]="Print Server Name :";
textArray0[iIndex++]="System Contact :";
textArray0[iIndex++]="System Location :";
textArray0[iIndex++]="Administrator's Password";
textArray0[iIndex++]="Account :";
textArray0[iIndex++]="admin";
textArray0[iIndex++]="Password :";
textArray0[iIndex++]="Confirm Password :";
// Translate                                  only "Save & Restart" is to be translated
textArray0[iIndex++]='<input type="button"  value="Save & Restart" onClick="return CheckPwd(';
// Begin don't translate
textArray0[iIndex++]="'restart.htm');";
textArray0[iIndex++]='">';
iIndex = 0;
// End don't translate
//ctcpip.htm
textArray2[iIndex++]="TCP/IP Settings";
textArray2[iIndex++]="Obtain TCP/IP settings automatically (use DHCP/BOOTP)";
textArray2[iIndex++]="Use the following TCP/IP settings";
textArray2[iIndex++]="IP Address :";
textArray2[iIndex++]="Subnet Mask :";
textArray2[iIndex++]="Default Gateway :";
textArray2[iIndex++]="Rendezvous (Bonjour) Settings";
textArray2[iIndex++]="Service :";
textArray2[iIndex++]="Disable";
textArray2[iIndex++]="Enable";
textArray2[iIndex++]="Service Name :";
// Translate                                  only "Save & Restart" is to be translated
textArray2[iIndex++]='<input type="button" value="Save & Restart" onClick="return SaveSetting(';
// Begin don't translate
textArray2[iIndex++]="'restart.htm');";
textArray2[iIndex++]='">';
iIndex = 0;
// End don't translate
//capple.htm
textArray3[iIndex++]="AppleTalk Settings";
textArray3[iIndex++]="AppleTalk Service :";
textArray3[iIndex++]="Disabled";
textArray3[iIndex++]="Enabled";
textArray3[iIndex++]="AppleTalk Zone Name :";
textArray3[iIndex++]="Port Name :";
textArray3[iIndex++]="Printer";
textArray3[iIndex++]="Type :";
textArray3[iIndex++]="Data Format :";
// Translate                                  only "Save & Restart" is to be translated
textArray3[iIndex++]='<input type=button value="Save & Restart" onClick="return SaveSetting(';
// Begin don't translate
textArray3[iIndex++]="'restart.htm');";
textArray3[iIndex++]='">';
// End don't translate

iIndex = 0;
//csnmp.htm
textArray4[iIndex++]="SNMP Community Settings";
textArray4[iIndex++]="SNMP Service :";
textArray4[iIndex++]="Disabled";
textArray4[iIndex++]="Enabled";
textArray4[iIndex++]="Support HP WebJetAdmin :";
textArray4[iIndex++]="Disabled";
textArray4[iIndex++]="Enabled";
textArray4[iIndex++]="SNMP Community Name 1 :";
textArray4[iIndex++]="Privilege :";
textArray4[iIndex++]="Read-Only";
textArray4[iIndex++]="Read-Write";
textArray4[iIndex++]="SNMP Community Name 2 :";
textArray4[iIndex++]="Privilege :";
textArray4[iIndex++]="Read-Only";
textArray4[iIndex++]="Read-Write";
textArray4[iIndex++]="SNMP Trap Settings";
textArray4[iIndex++]="Send SNMP Traps :";
textArray4[iIndex++]="Disabled";
textArray4[iIndex++]="Enabled";
textArray4[iIndex++]="Use Authentication Traps :";
textArray4[iIndex++]="Disabled";
textArray4[iIndex++]="Enabled";
textArray4[iIndex++]="Trap Address 1 :";
textArray4[iIndex++]="Trap Address 2 :";
// Translate                                  only "Save & Restart" is to be translated
textArray4[iIndex++]='<input type="button" value="Save & Restart" onClick="return SaveSetting(';
// Begin don't translate
textArray4[iIndex++]="'restart.htm');";
textArray4[iIndex++]='">';
iIndex = 0;
// End don't translate
//keyhelp.htm
//textArray5[iIndex++]="<b>WEP Key Format</b>";
//textArray5[iIndex++]="An alphanumeric character is 'a' through 'z', 'A' through 'Z', and '0' through '9'.";
//textArray5[iIndex++]="A hexadecimal digit is '0' through '9' and 'A' through 'F'.";
//textArray5[iIndex++]="Depending on the key format you select:";
//textArray5[iIndex++]="For 64-bit (sometimes called 40-bit) WEP encryption, enter the key which contains 5 alphanumeric characters or 10 hexadecimal digits. For example: AbZ12 (alphanumeric format) or ABCDEF1234 (hexadecimal format).";
//textArray5[iIndex++]="For 128-bit WEP encryption, enter the key which contains 13 alphanumeric characters or 26 hexadecimal digits.";
// Translate                                  only "Close" is to be translated
textArray5[iIndex++]='<INPUT TYPE=button VALUE=" Close " onClick="window.close()">';
iIndex = 0;
//browser.htm
//textArray6[iIndex++]="SSID";
//textArray6[iIndex++]="AP's MAC Address or BSSID";
//textArray6[iIndex++]="Channel";
//textArray6[iIndex++]="Type";
//textArray6[iIndex++]="WEP/WPA-PSK";
//textArray6[iIndex++]="Signal Strength";
// Translate                                  only "Rescan" is to be translated
//textArray6[iIndex++]='<INPUT TYPE=submit VALUE="Rescan">';
// Translate                                  only "Close" is to be translated
//textArray6[iIndex++]='<INPUT TYPE=button VALUE=" Close " onClick="window.close()">';
//iIndex = 0;
// End don't translate
// error.htm
textArray7[iIndex++]="ERROR";
textArray7[iIndex++]="Invalid IP Address";
textArray7[iIndex++]="Invalid Subnet Mask Address";
textArray7[iIndex++]="Invalid Gateway Address";
textArray7[iIndex++]="Invalid Polling Time Value";
textArray7[iIndex++]="Invalid Print Server Name";
textArray7[iIndex++]="Invalid File Server Name";
textArray7[iIndex++]="DHCP/BOOTP Server not found";
textArray7[iIndex++]="Invalid SNMP Trap IP Address";
textArray7[iIndex++]="Setup password and confirmed do not match";
textArray7[iIndex++]="Wrong firmware or upgrade failed";
textArray7[iIndex++]="Failed in importing the CFG file";
textArray7[iIndex++]="";
textArray7[iIndex++]="Go Back";
iIndex = 0;

// cnetware.htm
textArray8[iIndex++]="General Settings";
textArray8[iIndex++]="Print Server Name :";
textArray8[iIndex++]="Polling Time :";
textArray8[iIndex++]="&nbsp;seconds (min: 3 seconds, max: 29 seconds)";
textArray8[iIndex++]="Logon Password :";
textArray8[iIndex++]="NetWare NDS Settings";
textArray8[iIndex++]="Use NDS Mode :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Name of the NDS Tree :";
textArray8[iIndex++]="Name of the NDS Context :";
textArray8[iIndex++]="NetWare Bindery Settings";
textArray8[iIndex++]="Use Bindery Mode :";
textArray8[iIndex++]="Disabled";
textArray8[iIndex++]="Enabled";
textArray8[iIndex++]="Name of the File Server :";
textArray8[iIndex++]='<input type="button" value="Save & Restart" onClick="return SaveSetting(';
// Begin don't translate
textArray8[iIndex++]="'restart.htm');";
textArray8[iIndex++]='">';
iIndex = 0;

// csmb.htm
textArray9[iIndex++]="Workgroup";
textArray9[iIndex++]="SMB Service :";
textArray9[iIndex++]="Disabled";
textArray9[iIndex++]="Enabled";
textArray9[iIndex++]="Name :";
textArray9[iIndex++]="Shared Printer Name";
textArray9[iIndex++]="Printer :";
textArray9[iIndex++]='<input type="button" value="Save & Restart" onClick="return CheckSMB(';
// Begin don't translate
textArray9[iIndex++]="'restart.htm');";
textArray9[iIndex++]='">';
iIndex = 0;

// cservices.htm
textArray10[iIndex++]="Printing Method";
textArray10[iIndex++]="Use LPR/LPD :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="Use AppleTalk :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="Use IPP :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="Use SMB :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="Services";
textArray10[iIndex++]="Telnet :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="SNMP :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="E-mail and EOF Alert :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="HTTP :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="AirPrint :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="Scanner (AirScan / WSD) :";
textArray10[iIndex++]="Disabled";
textArray10[iIndex++]="Enabled";
textArray10[iIndex++]="ATTENTION: You can ONLY enable HTTP with the reset button after you disable it.";
textArray10[iIndex++]="AirPrint uses the device name (set in System settings) as the service name visible in iOS/macOS.";
textArray10[iIndex++]='<input type="button" value="Save & Restart" onClick="return SaveServices(';
// Begin don't translate
textArray10[iIndex++]="'restart.htm');";
textArray10[iIndex++]='">';
iIndex = 0;

// functions
// csystem.htm
function CheckPwd(szURL)
{
 	if(document.CSYSTEM.SetupPWD.value != document.CSYSTEM.ConfirmPWD.value && document.CSYSTEM.SetupPWD.value != "ZO__I-SetupPassword" )
	{
		alert("Administrator's Password and Confirmed Password do not match !");
		return false;
	}
	document.CSYSTEM.action=szURL;
	document.CSYSTEM.submit();
	return false;
}

// ctcpip.htm

// cnetware.htm

// capple.htm

// csnmp.htm

// csmb.htm
function CheckSMB(szURL)
{
	if(document.forms[0].SMBWorkGroup.value == '')
	{
		alert("ERROR! The workgroup name cannot be empty!");
		return false;
	}
	else
	{
		if ((document.forms[0].SMBPrint1.value == '')
			||(document.forms[0].SMBPrint2.value == '')
			||(document.forms[0].SMBPrint3.value == ''))
		{
			alert("ERROR! The SMB shared printer name cannot be empty!");
		 	return false;
		}
		else
		{
			if ((document.forms[0].SMBPrint1.value == document.forms[0].SMBPrint2.value)
				||(document.forms[0].SMBPrint2.value == document.forms[0].SMBPrint3.value)
				||(document.forms[0].SMBPrint1.value == document.forms[0].SMBPrint3.value))
		 	{
		 		alert("ERROR! The SMB shared printer name cannot be duplicate!");
		 		return false;
		 	}
			else
			{
				document.forms[0].action=szURL;
				document.forms[0].submit();
				return false;
			}
		}
	}
}

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
	document.write('<td><div class="MenuBtnStatus" onClick="location.href=');
	document.write("'system.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Status</div></div></td>');

	document.write('<td><div class="MenuBtnSetupSelected"');
	document.write(' style="position:relative;"><div>Setup</div></div></td>');

	document.write('<td><div class="MenuBtnMisc" onClick="location.href=');
	document.write("'default.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Misc</div></div></td>');

	document.write('<td><div class="MenuBtnRestart" onClick="location.href=');
	document.write("'reset.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>Restart</div></div></td>');
}

// out of English
