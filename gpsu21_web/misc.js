<html>
<head>
<title>Navigator</title>
</head>
<body>
<h2>Port Status</h2>
<hr>
LP1: 
<Script Language="JavaScript">
var strPort1 = "<!--# echo var="Port1"-->";
document.write(strPort1);
</script>
<br>
LP2: 
<Script Language="JavaScript">
var strPort2 = "<!--# echo var="Port2"-->";
document.write(strPort2);
</script>
<br>
LP3: 
<Script Language="JavaScript">
var strPort3 = "<!--# echo var="Port3"-->";
document.write(strPort3);
</script>
<br><br>
<!-- Waiting for job, Paper Out, Offline, Printing -->
<h2>Navigator</h2>
<hr>
<script language="JavaScript">
var objnav = window.navigator;
document.write("appCodeName= " + objnav.appCodeName + "<br>");
document.write("appMinorVersion= " + objnav.appMinorVersion + "<br>");
document.write("appName= " + objnav.appName + "<br>");
document.write("appVersion= " + objnav.appVersion + "<br>");
document.write("browserLanguage= " + objnav.browserLanguage + "<br>");
document.write("cookieEnabled= " + objnav.cookieEnabled + "<br>");
document.write("cpuClass= " + objnav.cpuClass + "<br>");
document.write("onLine= " + objnav.onLine + "<br>");
document.write("platform= " + objnav.platform + "<br>");
document.write("systemLanguage= " + objnav.systemLanguage + "<br>");
document.write("userAgent= " + objnav.userAgent + "<br>");
document.write("userLanguage= " + objnav.userLanguage + "<br>");
document.write("Language= " + objnav.language + "<br>");
</script>
</body>
</html>
