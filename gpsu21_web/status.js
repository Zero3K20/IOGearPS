// BrowserAdjustment
function BrowserAdjustment()
{
	if(window.navigator.appName == "Microsoft Internet Explorer")
		document.write('<td height="42">&nbsp;</td>');
	else
		document.write('<td height="42">&nbsp;</td>');
}

// Sections
function Section1()
{
	document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0">');
	document.write('<tr><td height="191" rowspan="6"><img src="images/mainView-1.jpg" width="448" height="191" /></td>');
	BrowserAdjustment();
	document.write('</tr><tr><td height="35">');
	//MainViewTitle();
	document.write('<table width=100% border=0><tr><td height=35><font color=#AACD03 style="font-size:20pt"><b>');
	showtab(tabindex++);
	document.write('</b></font></td><td><img src="images/arrows.jpg" height=33></td><td><font color=#AACD03 style="font-size:20pt"><b>');
	showtab(tabindex++);
	document.write('</b></font></td></tr></table>');
	document.write('</td></tr>');
	document.write('<tr><td height="18"><img src="images/mainView-2.jpg" width="576" height="18" /></td></tr>');
	document.write('<tr><td height="11"><img src="images/mainView-3.jpg" width="576" height="11" /></td></tr>');
	document.write('<tr><td height="35"><img src="images/mainView-4.jpg" width="576" height="35" /></td></tr>');
}

function Section2()
{
	document.write('<tr><td height="46"><table width="100%" align="left" cellpadding="0" cellspacing="0"><tr>');
	RowMenuBtn();
	document.write('</tr></table></td></tr></table></td></tr>');
}

function Section2r()
{
	document.write('<tr><td height="46"><table width="100%" align="left" cellpadding="0" cellspacing="0"><tr>');
	RowMenuBtn4();
	document.write('</tr></table></td></tr></table></td></tr>');
}

function Section3()
{
	document.write('<tr><td width="1024" height="46" bgcolor="#FFFFFF">');
	document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0">');
	document.write('<tr><td width="1000" height="46" bgcolor="#FFFFFF">');
}

function Section3a(strAba)
{
	document.write('<table width="50%" height="44" border="0" align="left" cellpadding="0" cellspacing="0" class="itemMenu" id="itemMenu">');
	document.write('<tr><td width="20%" height="44">&nbsp;</td><td width="18%" align="center" class="noChoose">');
	if(strAba == 'status')
		document.write('<a href="SYSTEM.HTM" target="_parent">');
	else
		document.write('<a href="CSYSTEM.HTM" target="_parent">');
	showtab(tabindex++);
	document.write('</a></td>');
	if(strAba == 'status'){
		document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
		document.write('<td width="18%" align="center" class="noChoose">');
		document.write('<a href="PRINTER.HTM" target="_parent">');
		showtab(tabindex++);
		document.write('</a></td>');
	}
	document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
	document.write('<td width="18%" align="center" class="noChoose">');
	if(strAba == 'status')
		document.write('<a href="TCPIP.HTM" target="_parent">');
	else
		document.write('<a href="CTCPIP.HTM" target="_parent">');
	showtab(tabindex++);
	document.write('</a></td>');
	document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
	document.write('<td width="18%" align="center" class="noChoose">');
	if(strAba == 'status')
		document.write('<a href="SERVICES.HTM" target="_parent">');
	else
		document.write('<a href="CSERVICES.HTM" target="_parent">');
	showtab(tabindex++);
	document.write('</a></td>');
	document.write('<td width="2" class="Separator"><div align="center">|</div></td></tr></table>');
}

function Section3b(strAba)
{
	document.write('<table width="40%" height="44" border="0" align="left" cellpadding="0" cellspacing="0" class="itemMenu" id="itemMenu2">');
	document.write('<tr><td width="18%" align="center" class="noChoose" id="netware">');
	if(strAba == 'status')
		document.write('<a href="NETWARE.HTM" target="_parent">');
	else
		document.write('<a href="CNETWARE.HTM" target="_parent">');
	showtab(tabindex++);
	document.write('</a></td>');
	document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
	document.write('<td width="18%" align="center" class="noChoose" id="apple">');
	if(strAba == 'status')
		document.write('<a href="APPLE.HTM" target="_parent">');
	else
		document.write('<a href="CAPPLE.HTM" target="_parent">');
	showtab(tabindex++);
	document.write('</a></td>');
	document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
	document.write('<td width="18%" align="center" class="noChoose" id="snmp">');
	if(strAba == 'status')
		document.write('<a href="SNMP.HTM" target="_parent">');
	else
		document.write('<a href="CSNMP.HTM" target="_parent">');
	showtab(tabindex++);
	document.write('</a></td>');
	document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
	document.write('<td width="18%" align="center" class="noChoose" id="smb">');
	if(strAba == 'status')
		document.write('<a href="SMB.HTM" target="_parent">');
	else
		document.write('<a href="CSMB.HTM" target="_parent">');
	showtab(tabindex++);
	document.write('</a></td>');
	document.write('<td width="22%">&nbsp;</td></tr></table>');
}

function Section3m1(strPage)
{
	document.write('<tr><td width="1024" height="46" bgcolor="#FFFFFF">');
	document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0">');
	document.write('<tr><td width="1000" height="46" bgcolor="#FFFFFF">');
	document.write('<table width="100%" height="44" border="0" align="left" cellpadding="0" cellspacing="0" class="itemMenu" id="itemMenu">');
	document.write('<tr><td width="20%" height="44">&nbsp;</td>');
	if(strPage == 'default')
	{
		document.write('<td width="28%" align="center" class="chooseThis">');
		showtab(tabindex++);
		document.write('</td>');
		document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
		document.write('<td width="28%" align="center" class="noChoose">');
		document.write('<a href="UPGRADE.HTM" target="_parent">');
		showtab(tabindex++);
		document.write('</a></td>');
	}
	else
	{
		document.write('<td width="28%" align="center" class="noChoose">');
		document.write('<a href="DEFAULT.HTM" target="_parent">');
		showtab(tabindex++);
		document.write('</a></td>');
		document.write('<td width="2" class="Separator"><div align="center">|</div></td>');
		document.write('<td width="28%" align="center" class="chooseThis">');
		showtab(tabindex++);
		document.write('</td>');
	}
	
	document.write('<td width="2" class="Separator"><div align="center">&nbsp;</div></td>');
	document.write('<td width="20%" height="44">&nbsp;</td></tr></table></td></tr></table></td></tr>');
}

function Section3m2()
{
	document.write('<tr><td width="1024" height="46" bgcolor="#FFFFFF">');
	document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0">');
	document.write('<tr><td width="1000" height="46" bgcolor="#FFFFFF">');
	document.write('<table width="100%" height="44" border="0" align="left" cellpadding="0" cellspacing="0" class="itemMenu" id="itemMenu">');
	document.write('<tr><td width="100%" height="44">&nbsp;</td></tr></table></td></tr></table></td></tr>');
}

function Section4()
{
	document.write('<table height="44" border="0" align="right" cellpadding="0" cellspacing="0" class="itemMenu" id="itemMenu2">');
	document.write('<tr><td>');
	document.write('<img src="images/btn-more.jpg" name="BtnMore" width="9" height="16" id="BtnMore" style=cursor:pointer;display:"" onclick="MODE_CHANG();">');
	document.write('<img src="images/btn-less.jpg" name="BtnLess" width="9" height="16" id="BtnLess" style=cursor:pointer;display:none onclick="MODE_CHANG();">');
	document.write('</td><td width="20">&nbsp;</td>');
	document.write('</tr></table>');
	document.write('</td>');
}

function Section5(strPage)
{
	document.write('<tr><td width="1024" height="100" bgcolor="#AACD03">');
	document.write('<table width="960" border="0" align="center" cellpadding="0" cellspacing="15" id="info">');
	document.write('<tr><td width="41" height="40" valign="bottom" class="infoImg">');
	document.write('<BR><img src="images/InfoImg.jpg" width="41" height="34" /></td>');
	document.write('<td valign="middle" class="infoT">');
	showhead(strPage);
	document.write('</td></tr></table></td></tr>');
}
