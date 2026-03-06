/*
 * top.js — Navigation layout and utility functions for the GPSU21 web UI.
 *
 * Provides Section1()–Section5(), findcook(), and loads the language-specific
 * text arrays (via setup-z.js → setup-c.js / setup-e.js) so that all
 * showtext*() calls on the setup configuration pages work correctly.
 *
 * This file must be included AFTER setup.js (which defines tabArray/textArrayX
 * for the status pages) and BEFORE effect_setting.js in all setup HTML pages.
 */

/* Load the setup-z.js framework, which in turn loads the language-appropriate
 * setup-{c|e|z}.js to populate textArray10 (cservices/cnetware), textArray2
 * (ctcpip), etc. and define showtext2(), showtext10(), showtab(), showhead(). */
document.write('<SCRIPT language="JavaScript" src="setup-z.js"><\/SCRIPT>');

/* ── Section layout helpers ─────────────────────────────────────────────── *
 *
 * These functions emit the structural HTML for the page header and navigation
 * bar.  They are called in sequence at the top of every setup HTML page:
 *   Section1()           — product logo / header row
 *   Section2()           — main tab bar (Status | Setup | Misc | Restart)
 *   Section3()           — layout spacer / start of content area
 *   Section3a('setup')   — secondary sub-tab context switch (used by sub-pages)
 *   Section3b('setup')   — alternative secondary context (used by top-level pages)
 *   Section4()           — separator line between nav and content
 *   Section5(pageName)   — page-specific help text row
 * ────────────────────────────────────────────────────────────────────────── */

function Section1() {
    document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0">');
    document.write('<tr><td>');
    document.write('<SCRIPT language="JavaScript">MainViewTitle();<\/SCRIPT>');
    document.write('<\/td><\/tr><\/table>');
}

function Section2() {
    document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0" class="MenuBtnBG">');
    document.write('<tr><td>');
    document.write('<SCRIPT language="JavaScript">RowMenuBtn();<\/SCRIPT>');
    document.write('<\/td><\/tr><\/table>');
}

function Section3() {
    document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0">');
    document.write('<tr><td height="44">');
}

function Section3a(ctx) {
    /* Section3a is used by sub-pages (capple.htm, csmb.htm, etc.) that sit
     * under the "Services" umbrella.  The sub-tab table is inlined in the
     * HTML after this call.  This function closes the outer row started by
     * Section3 if needed. */
    void ctx; /* suppress unused-variable lint warnings */
}

function Section3b(ctx) {
    /* Section3b is used by top-level setup pages (csystem.htm, cnetware.htm)
     * to close the 3-tab navigation row opened by Section3. */
    void ctx;
    document.write('<\/td><\/tr><\/table>');
}

function Section4() {
    document.write('<table width="1024" border="0" align="center" cellpadding="0" cellspacing="0">');
    document.write('<tr><td height="4" bgcolor="#7E8382"><\/td><\/tr><\/table>');
}

function Section5(pageName) {
    document.write('<tr><td width="1024" bgcolor="#CBCDCC">');
    document.write('<table width="900" border="0" align="center" cellpadding="4">');
    document.write('<tr><td class="info">');
    document.write('<SCRIPT language="JavaScript">showhead("' + pageName + '");<\/SCRIPT>');
    document.write('<\/td><\/tr><\/table><\/td><\/tr>');
}

/* ── Cookie helper ──────────────────────────────────────────────────────── */

function findcook() {
    /* Read the 'lang' cookie (set by the language-detection code) and
     * optionally override browserLangu.  In this implementation the language
     * is already selected by setup-z.js so this is a no-op. */
}
