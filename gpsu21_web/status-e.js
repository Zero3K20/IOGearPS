//variable
tabindex = 0;
textindex = 0;

var iIndex = 0;

tabArray=['GPSU21','USB埠印表伺服器','系統','印表機','TCP/IP','服務','NetWare','AppleTalk','SNMP','SMB',''];
//Language : Chinese

//system.htm
headArray[iIndex++] = "<BR>本頁可以顯示有關此印表伺服器的狀態與資訊。<BR>";
//printer.htm
headArray[iIndex++] = "<BR>本頁可以顯示與印表伺服器連接的印表機的狀態與資訊。<BR>附註: 假如您的印表機不支援雙向列印功能, 印表機的廠牌, 型號及列印語言將不會被顯示出來。";
//tcpip.htm
headArray[iIndex++] = "<BR>本頁顯示目前此印表伺服器的 TCP/IP 設定值。<BR>";
//services.htm
headArray[iIndex++] = "<BR>本頁可以讓你修改此印表伺服器服務的設定。<BR>";
//netware.htm
headArray[iIndex++] = "<BR>本頁顯示目前此印表伺服器的 NetWare 設定值。<BR>";
//apple.htm
headArray[iIndex++] = "<BR>本頁顯示目前此印表伺服器的 AppleTalk 設定值。<BR>";
//snmp.htm
headArray[iIndex++] = "<BR>本頁顯示目前此印表伺服器的 SNMP 設定值。<BR>";
//smb.htm
headArray[iIndex++] = "<BR>本頁顯示目前此印表伺服器在微軟網路芳鄰中的印表機分享設定。<BR>";
iIndex = 0;



//system.htm
textArray0[iIndex++]="系統資訊";
textArray0[iIndex++]="裝置名稱 :";
textArray0[iIndex++]="聯絡人 :";
textArray0[iIndex++]="裝置位置 :";
textArray0[iIndex++]="開機時間 :";
textArray0[iIndex++]="韌體版本 :";
textArray0[iIndex++]="網路卡位址 :";
textArray0[iIndex++]="E-mail 警示 :";
textArray0[iIndex++]="停用";
textArray0[iIndex++]="啟用";
//printjob.htm
textArray0[iIndex++]="目前的列印工作";
textArray0[iIndex++]="工作編號";
textArray0[iIndex++]="使用者";
textArray0[iIndex++]="花費時間";
textArray0[iIndex++]="通訊協定";
textArray0[iIndex++]="列印埠";
textArray0[iIndex++]="狀態";
textArray0[iIndex++]="位元組數";
textArray0[iIndex++]="觀看列印工作紀錄";
iIndex = 0;

//printer.htm
textArray1[iIndex++]="印表機";
textArray1[iIndex++]="印表機廠牌 :";
textArray1[iIndex++]="印表機型號 :";
textArray1[iIndex++]="支援的列印語言 :";
textArray1[iIndex++]="目前狀態 :";
textArray1[iIndex++]="待機中";
textArray1[iIndex++]="缺紙";
textArray1[iIndex++]="未連接或印表機離線";
textArray1[iIndex++]="列印中";
textArray1[iIndex++]="列印速度 :";
textArray1[iIndex++]="快";
textArray1[iIndex++]="中等";
textArray1[iIndex++]="慢";
iIndex = 0;

//netware.htm
textArray2[iIndex++]="基本設定";
textArray2[iIndex++]="印表伺服器名稱 :";
textArray2[iIndex++]="輪詢時間 :";
textArray2[iIndex++]="秒";
textArray2[iIndex++]="NetWare NDS 設定";
textArray2[iIndex++]="使用 NDS 模式 :";
textArray2[iIndex++]="停用";
textArray2[iIndex++]="啟用";
textArray2[iIndex++]="NDS Tree 名稱 :";
textArray2[iIndex++]="NDS Context 名稱 :";
textArray2[iIndex++]="目前狀態 :";
textArray2[iIndex++]="未連接";
textArray2[iIndex++]="已連接";
textArray2[iIndex++]="NetWare Bindery 設定";
textArray2[iIndex++]="使用 Bindery 模式 :";
textArray2[iIndex++]="停用";
textArray2[iIndex++]="啟用";
textArray2[iIndex++]="檔案伺服器名稱 :";
textArray2[iIndex++]="目前狀態 :";
textArray2[iIndex++]="未連接";
textArray2[iIndex++]="已連接";
iIndex = 0;
//tcpip.htm
textArray3[iIndex++]="TCP/IP 設定";
textArray3[iIndex++]="使用 DHCP/BOOTP :";
textArray3[iIndex++]="停用 (使用固定 IP)";
textArray3[iIndex++]="啟用 (自動取得 IP)";
textArray3[iIndex++]="IP 位址 :";
textArray3[iIndex++]="子網路遮罩 :";
textArray3[iIndex++]="閘道器 :";
//randvoo.htm
textArray3[iIndex++]="Rendezvous (Bonjour) 設定";
textArray3[iIndex++]="狀態 :";
//textArray3[iIndex++]="停用";
//textArray3[iIndex++]="啟用";
textArray3[iIndex++]="服務名稱 :";
iIndex = 0;
//apple.htm
textArray4[iIndex++]="AppleTalk 設定";
textArray4[iIndex++]="AppleTalk 區域名稱 :";
textArray4[iIndex++]="連接埠";
textArray4[iIndex++]="連接埠名稱 :";
textArray4[iIndex++]="印表機形式 :";
textArray4[iIndex++]="資料格式 :";
iIndex = 0;
//snmp.htm
textArray5[iIndex++]="SNMP 群體設定";
textArray5[iIndex++]="群體 1 :";
textArray5[iIndex++]="只能讀";
textArray5[iIndex++]="讀寫皆可";
textArray5[iIndex++]="群體 2 :";
textArray5[iIndex++]="只能讀";
textArray5[iIndex++]="讀寫皆可";
textArray5[iIndex++]="SNMP 陷阱設定";
textArray5[iIndex++]="使用陷阱補抓 :";
textArray5[iIndex++]="停用";
textArray5[iIndex++]="啟用";
textArray5[iIndex++]="傳送確認陷阱 :";
textArray5[iIndex++]="停用";
textArray5[iIndex++]="啟用";
textArray5[iIndex++]="陷阱目標 IP 位址 1 :";
textArray5[iIndex++]="陷阱目標 IP 位址 2 :";
iIndex = 0;

//joblog.htm
// Translate                                  only "Refresh " is to be translated
textArray6[iIndex++]='<input type=button value=" 更新 " onClick="window.location.reload()">';
textArray6[iIndex++]="列印工作紀錄";
textArray6[iIndex++]="工作編號";
textArray6[iIndex++]="使用者";
textArray6[iIndex++]="花費時間";
textArray6[iIndex++]="通訊協定";
textArray6[iIndex++]="列印埠";
textArray6[iIndex++]="狀態";
textArray6[iIndex++]="位元組數";
// Translate                                  only "Close " is to be translated
textArray6[iIndex++]='<input type=button value=" 關閉 " onClick="window.close()">';
iIndex = 0;


//smb.htm
textArray7[iIndex++]="工作群組";
textArray7[iIndex++]="名稱 :";
textArray7[iIndex++]="印表機共用名稱";
textArray7[iIndex++]="連接埠 :";
iIndex = 0;

//services.htm
textArray8[iIndex++]="列印方式";
textArray8[iIndex++]="使用 NetWare Bindery 列印 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="使用 NetWare NDS 列印 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="使用 LPR/LPD 列印 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="使用 AppleTalk 列印 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="使用 IPP 列印 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="使用 SMB 列印 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="服務";
textArray8[iIndex++]="Telnet :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="SNMP :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="E-mail 和結束頁警示 :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";
textArray8[iIndex++]="HTTP :";
textArray8[iIndex++]="停用";
textArray8[iIndex++]="啟用";

// Title or Model Name
function TitleModelName()
{
	document.write('<title>GPSU21 USB印表伺服器</title>');
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
	document.write(' style="position:relative;"><div>狀態</div></div></td>');

	document.write('<td><div class="MenuBtnSetup" onClick="location.href=');
	document.write("'csystem.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>設定</div></div></td>');

	document.write('<td><div class="MenuBtnMisc" onClick="location.href=');
	document.write("'default.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>其它</div></div></td>');

	document.write('<td><div class="MenuBtnRestart" onClick="location.href=');
	document.write("'reset.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>重新啟動</div></div></td>');
}

// out of Chinese
