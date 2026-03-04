

//variable
tabindex = 0;
textindex = 0;
var iIndex = 0;

//Language : Chinese
tabArray=['IOGEAR GPSU21','USB埠印表伺服器','回復出廠值','韌體升級'];

	//upgrade.htm
headArray[iIndex++] ="<br>本頁可以讓您升級印表伺服器的韌體。<br><br><font color=red>附註:</font> 在執行升級之前, 請確定您的韌體是正確的。假如您不知道該用哪種韌體, 請與廠商聯絡以尋求技術上的支援。";
iIndex = 0;

	//default.htm
textArray1[iIndex++] = "<BR>點擊「<b>確定</b>」 以回復出廠的預設值。<br><br><FONT CLASS=F1 COLOR=#FF3300><B>請注意! 所有目前的設定值都將被抹除。</B></FONT>";
textArray1[iIndex++] = "回復出廠值 (不包括TCP/IP 設定參數)";
textArray1[iIndex++] = "重新啟動後，各項設定將被抹除，但仍然可用目前的IP位置連到設定頁面。";
textArray1[iIndex++] = "回復出廠值 (包括TCP/IP設定參數)";
textArray1[iIndex++] = "重新啟動後，各項設定將被抹除，並回復預設IP位置：192.168.0.10";
// Translate                               Only OK is to be translated
textArray1[iIndex++] = '<input type=button  value="&nbsp;&nbsp;確定&nbsp;&nbsp;" onClick="return SaveIPSetting(';
// Begin don't translate
textArray1[iIndex++] = "'drestart.htm');";
textArray1[iIndex++] = '">';
iIndex = 0;
// End don't translate

//upgrade.htm
textArray2[iIndex++]="韌體升級";
textArray2[iIndex++]="選擇檔案:";
// Begin don't translate
textArray2[iIndex++]='<input type=button value="韌體升級" onClick="return WebUpgrade()">';
iIndex = 0;
// End don't translate

//reset.htm
textArray3[iIndex++]="本頁可以讓您重新啟動印表伺服器。<br>";
textArray3[iIndex++]="重新啟動印表伺服器<br>";
textArray3[iIndex++]="你想要重新啟動印表伺服器嗎?<br>";
// Translate                               Only OK is to be translated
textArray3[iIndex++]='<input type=button value="&nbsp;&nbsp;確定&nbsp;&nbsp;" onClick="window.location=';
// Begin don't translate
textArray3[iIndex++]="'restart.htm'";
textArray3[iIndex++]='">';
iIndex = 0;
// End don't translate

	//restart.htm
textArray4[iIndex++] = "重新啟動中 ...";
textArray4[iIndex++] = "請等待印表伺服器重新啟動。";
iIndex = 0;
	//urestart.htm
textArray5[iIndex++] = "升級成功 !";
textArray5[iIndex++] = "韌體升級成功後, 印表伺服器將會自動重新啟動。請等待印表伺服器重新啟動。";
iIndex = 0;
	//drestart.htm
textArray6[iIndex++] = "回復為出廠的設定值中 ...";
textArray6[iIndex++] = " 回復為出廠的設定值後, 印表伺服器將會自動重新啟動。<BR><BR>請等待印表伺服器重新啟動。";
iIndex = 0;

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
	document.write('<td><div class="MenuBtnStatus" onClick="location.href=');
	document.write("'system.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>狀態</div></div></td>');

	document.write('<td><div class="MenuBtnSetup" onClick="location.href=');
	document.write("'csystem.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>設定</div></div></td>');

	document.write('<td><div class="MenuBtnMiscSelected"');
	document.write(' style="position:relative;"><div>其它</div></div></td>');

	document.write('<td><div class="MenuBtnRestart" onClick="location.href=');
	document.write("'reset.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>重新啟動</div></div></td>');
}

function RowMenuBtn4()
{
	document.write('<td><div class="MenuBtnStatus" onClick="location.href=');
	document.write("'system.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>狀態</div></div></td>');

	document.write('<td><div class="MenuBtnSetup" onClick="location.href=');
	document.write("'csystem.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>設定</div></div></td>');

	document.write('<td><div class="MenuBtnMisc" onClick="location.href=');
	document.write("'default.htm'");
	document.write(';" style="cursor:pointer;position:relative;"><div>其它</div></div></td>');

	document.write('<td><div class="MenuBtnRestartSelected"');
	document.write(' style="position:relative;"><div>重新啟動</div></div></td>');
}

// out of Chinese
