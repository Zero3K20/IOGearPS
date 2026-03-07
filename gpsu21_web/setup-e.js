
//variable
tabindex = 0;
textindex = 0;
var iIndex = 0;

//Language : Chinese
tabArray=['IOGEAR GPSU21','USB埠印表伺服器','系統','TCP/IP','服務','NetWare','AppleTalk','SNMP','SMB','',''];

//csystem
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器的基本設定。<br>";
//ctcpip.htm
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器 TCP/IP 的設定。";
//cservices.htm
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器服務相關的設定項目。";
//cnetware.htm
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器 NetWare 相關的設定項目。";
//capple.htm
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器 AppleTalk 相關的設定項目。<br>";
//csnmp.htm
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器的 SNMP 的設定。";
//csmb.htm
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器的微軟網路芳鄰印表機分享設定。";
iIndex = 0;

//csystem.htm
textArray0[iIndex++]="E-mail 警示設定";
textArray0[iIndex++]="E-mail 警示:";
textArray0[iIndex++]="停用";
textArray0[iIndex++]="啟用";
textArray0[iIndex++]="外寄郵件伺服器 IP 地址:";
textArray0[iIndex++]="管理者 E-mail 信箱:";
textArray0[iIndex++]="系統設定";
textArray0[iIndex++]="裝置名稱 :";
textArray0[iIndex++]="連絡人 :";
textArray0[iIndex++]="裝置位置 :";
textArray0[iIndex++]="系統管理者密碼";
textArray0[iIndex++]="管理者帳號 :";
textArray0[iIndex++]="admin";
textArray0[iIndex++]="密碼 :";
textArray0[iIndex++]="密碼確認 :";
// Translate                                  only "Save & Restart" is to be translated
textArray0[iIndex++]='<input type="button"  value="儲存並重新啟動" onClick="return CheckPwd(';
// Begin don't translate
textArray0[iIndex++]="'restart.htm');";
textArray0[iIndex++]='">';
iIndex = 0;
// End don't translate
//ctcpip.htm
textArray2[iIndex++]="TCP/IP 設定";
textArray2[iIndex++]="自動取得 IP 位址 (使用 DHCP/BOOTP)";
textArray2[iIndex++]="指定 IP 位址";
textArray2[iIndex++]="IP 位址 :";
textArray2[iIndex++]="子網路遮罩 :";
textArray2[iIndex++]="預設閘道器 :";
textArray2[iIndex++]="Rendezvous (Bonjour) 設定";
textArray2[iIndex++]="服務 :";
textArray2[iIndex++]="停用";
textArray2[iIndex++]="啟用";
textArray2[iIndex++]="服務名稱 :";
// Translate                                  only "Save & Restart" is to be translated
textArray2[iIndex++]='<input type="button" value="儲存並重新啟動" onClick="return SaveSetting(';
// Begin don't translate
textArray2[iIndex++]="'restart.htm');";
textArray2[iIndex++]='">';
iIndex = 0;
// End don't translate
//capple.htm
textArray3[iIndex++]="AppleTalk 設定";
textArray3[iIndex++]="AppleTalk 服務 :";
textArray3[iIndex++]="停用";
textArray3[iIndex++]="啟用";
textArray3[iIndex++]="AppleTalk 區域名稱 :";
textArray3[iIndex++]="連接埠名稱 :";
textArray3[iIndex++]="連接埠";
textArray3[iIndex++]="印表機形式 :";
textArray3[iIndex++]="資料格式 :";
// Translate                                  only "Save & Restart" is to be translated
textArray3[iIndex++]='<input type=button value="儲存並重新啟動" onClick="return SaveSetting(';
// Begin don't translate
textArray3[iIndex++]="'restart.htm');";
textArray3[iIndex++]='">';
// End don't translate

iIndex = 0;
//csnmp.htm
textArray4[iIndex++]="SNMP 群體設定";
textArray4[iIndex++]="SNMP 服務 :";
textArray4[iIndex++]="停用";
textArray4[iIndex++]="啟用";
textArray4[iIndex++]="支援 HP WebJetAdmin :";
textArray4[iIndex++]="停用";
textArray4[iIndex++]="啟用";
textArray4[iIndex++]="群體名稱 1 :";
textArray4[iIndex++]="群體權限 :";
textArray4[iIndex++]="只能讀";
textArray4[iIndex++]="讀寫皆可";
textArray4[iIndex++]="群體名稱 2 :";
textArray4[iIndex++]="群體權限 :";
textArray4[iIndex++]="只能讀";
textArray4[iIndex++]="讀寫皆可";
textArray4[iIndex++]="SNMP 陷阱設定";
textArray4[iIndex++]="使用陷阱補抓 :";
textArray4[iIndex++]="停用";
textArray4[iIndex++]="啟用";
textArray4[iIndex++]="傳送確認陷阱 :";
textArray4[iIndex++]="停用";
textArray4[iIndex++]="啟用";
textArray4[iIndex++]="陷阱目標 IP 位址 1 :";
textArray4[iIndex++]="陷阱目標 IP 位址 2 :";
// Translate                                  only "Save & Restart" is to be translated
textArray4[iIndex++]='<input type="button" value="儲存並重新啟動" onClick="return SaveSetting(';
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
textArray5[iIndex++]='<INPUT TYPE=button VALUE=" 關閉 " onClick="window.close()">';
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
textArray7[iIndex++]="錯誤";
textArray7[iIndex++]="IP 地址錯誤";
textArray7[iIndex++]="子網路遮罩錯誤";
textArray7[iIndex++]="閘道器 IP 地址錯誤";
textArray7[iIndex++]="輪詢週期值錯誤";
textArray7[iIndex++]="印表伺服器名稱錯誤";
textArray7[iIndex++]="NetWare 檔案伺服器名稱錯誤";
textArray7[iIndex++]="找不到 DHCP/BOOTP 伺服器";
textArray7[iIndex++]="SNMP 陷阱 IP 地址錯誤";
textArray7[iIndex++]="密碼錯誤";
textArray7[iIndex++]="韌體有問題或升級失敗";
textArray7[iIndex++]="CFG.bin 匯入失敗";
textArray7[iIndex++]="";
textArray7[iIndex++]="回到上一頁";
iIndex = 0;

// cnetware.htm
textArray8[iIndex++]="基本設定";
textArray8[iIndex++]="印表伺服器名稱 :";
textArray8[iIndex++]="輪詢時間 :";
textArray8[iIndex++]="&nbsp;秒 (最小: 3 秒, 最大: 29 秒)";
textArray8[iIndex++]="登入 NetWare 的密碼 :";
textArray8[iIndex++]="NetWare NDS 設定";
textArray8[iIndex++]="使用 NDS 模式 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="NDS Tree 名稱 :";
textArray8[iIndex++]="NDS Context 名稱 :";
textArray8[iIndex++]="NetWare Bindery 設定";
textArray8[iIndex++]="使用 Bindery 模式 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="檔案伺服器名稱 :";
textArray8[iIndex++]='<input type="button" value="儲存並重新啟動" onClick="return SaveSetting(';
// Begin don't translate
textArray8[iIndex++]="'restart.htm');";
textArray8[iIndex++]='">';
iIndex = 0;

// csmb.htm
textArray9[iIndex++]="工作群組";
textArray9[iIndex++]="SMB 服務 :";
textArray9[iIndex++]="停用";
textArray9[iIndex++]="啟用";
textArray9[iIndex++]="名稱 :";
textArray9[iIndex++]="印表機共用名稱";
textArray9[iIndex++]="連接埠 :";
textArray9[iIndex++]='<input type="button" value="儲存並重新啟動" onClick="return CheckSMB(';
// Begin don't translate
textArray9[iIndex++]="'restart.htm');";
textArray9[iIndex++]='">';
iIndex = 0;

//cservices.htm
textArray10[iIndex++]="列印方式";
textArray10[iIndex++]="使用 LPR/LPD 列印 :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="使用 AppleTalk 列印 :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="使用 IPP 列印 :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="使用 SMB 列印 :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="服務";
textArray10[iIndex++]="Telnet :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="SNMP :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="E-mail 和結束頁警示 :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="HTTP :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="AirPrint :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="AirScan (掃描器) :";
textArray10[iIndex++]="停用";
textArray10[iIndex++]="啟用";
textArray10[iIndex++]="請注意: 在您停用 HTTP 後, 只能用 Reset 鍵啟用它.";
textArray10[iIndex++]="AirPrint 使用系統設定中的裝置名稱作為 iOS/macOS 顯示的服務名稱.";
textArray10[iIndex++]='<input type="button" value="儲存並重新啟動" onClick="return SaveServices(';
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
		alert("系統管理者的密碼與密碼確認不符 !");
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
		alert("錯誤! 工作群組名稱不能空白!");
		return false;
	}
	else
	{
		if(document.forms[0].SMBPrint1.value == '')
		{
			alert("錯誤! SMB 共享印表機名稱不能空白!");
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

// Title or Model Name
function TitleModelName()
{
	document.write('<title>GPSU21 USB印表伺服器</title>');
}

// Character Encoding
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
	document.write(';" style="cursor:pointer;position:relative;"><div>狀態</div></div></td>');

	document.write('<td><div class="MenuBtnSetupSelected"');
	document.write(' style="position:relative;"><div>設定</div></div></td>');

	document.write('<td><div class="MenuBtnMisc" onClick="location.href=');
	document.write("'default.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>其它</div></div></td>');

	document.write('<td><div class="MenuBtnRestart" onClick="location.href=');
	document.write("'reset.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>重新啟動</div></div></td>');
}

// out of Chinese
