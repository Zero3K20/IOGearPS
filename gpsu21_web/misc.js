Len_html=1;Len_tab=8;
Len_text1=8;Len_text2=3;Len_text3=6;
Len_text4=2;Len_text5=2;Len_text6=2;
htmArray=new Array(Len_html);
tabArray=new Array(Len_tab);headArray=new Array(Len_html);
textArray1=new Array(Len_text1);textArray2=new Array(Len_text2);
textArray3=new Array(Len_text3);textArray4=new Array(Len_text4);
textArray5=new Array(Len_text5);textArray6=new Array(Len_text6);
htmArray=['upgrade.htm'];
browserLangu='e';
function adjuestlanguage(){
var l=navigator.browserLanguage||navigator.language||'';
if(/zh.?(TW|HK|MO|HANT)/i.test(l))browserLangu='z';
else if(/zh.?(CN|SG|HANS)/i.test(l))browserLangu='c';
}
function showtab(i){document.write(tabArray[i]);return true;}
function showhead(n){
for(var i=0;i<Len_html;i++){if(n.toLowerCase()==htmArray[i])break;}
document.write(headArray[i]);return true;
}
function showtext1(i){document.write(textArray1[i]);return true;}
function showtext2(i){document.write(textArray2[i]);return true;}
function showtext3(i){document.write(textArray3[i]);return true;}
function showtext4(i){document.write(textArray4[i]);return true;}
function showtext5(i){document.write(textArray5[i]);return true;}
function showtext6(i){document.write(textArray6[i]);return true;}
adjuestlanguage();
document.write('<SCRIPT language="JavaScript" src="misc-'+browserLangu+'.js"></SCRIPT>');
