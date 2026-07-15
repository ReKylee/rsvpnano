#include "sync/CompanionSyncManager.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "board/BoardStorage.h"

#include "display/ThemeStore.h"
#include "fonts/FontCatalog.h"
#include "fonts/RFont4Format.h"
#include "settings/PreferenceSpecs.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/IndexedBook.h"
#include "storage/index/ReadingProgress.h"
#include "text/AsciiText.h"
#include "ui/Localization.h"

namespace {

    namespace pref = settings::prefs;

    constexpr const char* kMdnsName = "rsvp-nano";
    constexpr size_t kMaxMetadataLineChars = 160;
    constexpr size_t kMaxSettingsPatchBytes = 8192;
    constexpr size_t kMaxRssFeedsPatchBytes = 4096;
    constexpr size_t kMaxThemeUploadBytes = 4096;
    constexpr size_t kMaxFontUploadBytes = 2UL * 1024UL * 1024UL;
    constexpr size_t kMaxRssFeeds = 24;
    constexpr size_t kBrightnessCount = 20;

    bool ensureLibraryDirectories() {
        return StorageFiles::ensureDirectory(StoragePaths::kBooksPath, "sync")
            && StorageFiles::ensureDirectory(StoragePaths::kBookFilesPath, "sync")
            && StorageFiles::ensureDirectory(StoragePaths::kArticleFilesPath, "sync");
    }

    bool ensureThemeDirectory() {
        return StorageFiles::ensureDirectory(StoragePaths::kThemesPath, "sync");
    }

    bool ensureFontDirectory() {
        return StorageFiles::ensureDirectory(StoragePaths::kFontsPath, "sync");
    }

    bool ensureFontFamilyDirectory(const String& family) {
        if (!ensureFontDirectory()) {
            return false;
        }
        return StorageFiles::ensureDirectory((String(StoragePaths::kFontsPath) + "/" + family).c_str(), "sync");
    }

    String readSmallTextFile(const String& path) {
        File file = Board::Storage::filesystem().open(path, FILE_READ);
        String text;
        if (!file) {
            return text;
        }
        text.reserve(std::min(static_cast<size_t>(file.size()), kMaxThemeUploadBytes));
        while (file.available() && text.length() < kMaxThemeUploadBytes) {
            text += static_cast<char>(file.read());
        }
        file.close();
        return text;
    }

    bool replaceUploadedFile(const String& tmpPath, const String& finalPath) {
        const String backupPath = finalPath + ".bak";
        Board::Storage::filesystem().remove(backupPath);

        const bool hadFinal = StorageFiles::fileExists(finalPath.c_str());
        if (hadFinal && !Board::Storage::filesystem().rename(finalPath, backupPath)) {
            return false;
        }

        if (Board::Storage::filesystem().rename(tmpPath, finalPath)) {
            if (hadFinal) {
                Board::Storage::filesystem().remove(backupPath);
            }
            return true;
        }

        if (hadFinal) {
            Board::Storage::filesystem().rename(backupPath, finalPath);
        }
        return false;
    }

    const char kWebCompanionHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RSVP Nano Companion</title>
<style>
:root{color-scheme:dark;--bg:#0c1110;--fg:#f5f1e8;--muted:#a7aaa0;--line:#2d3430;--card:#151b18;--accent:#78d5b1;--accentInk:#07110e;--accent2:#ff9b73;--soft:#1d2924}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top left,#18241f 0,#0c1110 38%);color:var(--fg);font:15px/1.45 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
header{position:sticky;top:0;z-index:2;background:rgba(12,17,16,.92);backdrop-filter:blur(14px);border-bottom:1px solid var(--line);padding:14px 16px 10px}
h1{font-size:1.15rem;margin:0 0 10px}.tabs{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:6px}
button,.button{border:1px solid var(--line);border-radius:8px;background:#111714;color:var(--fg);padding:9px 11px;font:inherit}
button.primary,.button.primary{background:var(--accent);border-color:var(--accent);color:var(--accentInk);font-weight:700}button.danger{color:var(--accent2)}
.tabs button{white-space:nowrap;padding:8px 6px}.tabs button.active{background:var(--fg);color:var(--bg)}
main{max-width:980px;margin:0 auto;padding:16px}.page{display:none}.page.active{display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px;margin-bottom:12px}
h2{font-size:1.05rem;margin:0 0 10px}h3{font-size:.95rem;margin:0 0 8px}.muted{color:var(--muted)}.status{padding:10px 12px;border-radius:8px;background:var(--soft);margin-bottom:12px}
label{display:block;font-weight:650;margin:10px 0 5px}input,textarea,select{width:100%;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit;padding:9px}
textarea{min-height:180px;resize:vertical}.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.row>*{flex:1}.row button{flex:0 0 auto}
.item{border-top:1px solid var(--line);padding:10px 0}.item:first-child{border-top:0}.item-title{font-weight:700}.item-meta{color:var(--muted);font-size:.9rem}
ul{padding-left:20px}code{background:var(--soft);border-radius:4px;padding:1px 4px}
</style>
</head>
<body>
<header>
<h1>RSVP Nano Companion</h1>
<nav class="tabs">
<button data-tab="books" class="active">Books</button>
<button data-tab="articles">Articles</button>
<button data-tab="settings">Settings</button>
<button data-tab="rss">RSS</button>
<button data-tab="help">Help</button>
</nav>
</header>
<main>
<div id="status" class="status">Connecting to reader...</div>

<section id="books" class="page active">
<div class="grid">
<div class="card"><h2>Upload Book</h2>
<p class="muted">For best EPUB/HTML/Markdown conversion, use the hosted web converter first, then upload the finished <code>.rsvp</code> file here wirelessly.</p>
<label>Book file</label><input id="bookFileInput" type="file" accept=".rsvp,.txt,.epub">
<p><button class="primary" id="uploadBookButton">Upload book</button></p>
</div>
<div class="card"><h2>Reader</h2><div id="infoBox" class="muted">No reader info yet.</div><p><button id="refreshBooksButton">Refresh books</button></p></div>
</div>
<div class="card"><h2>Books</h2><div id="booksList" class="muted">Loading...</div></div>
</section>

<section id="articles" class="page">
<div class="grid">
<div class="card"><h2>New Article</h2>
<label>Title</label><input id="articleTitle" placeholder="Article title">
<label>Author or source</label><input id="articleAuthor" placeholder="Website or author">
<label>Body</label><textarea id="articleBody" placeholder="Paste article text here"></textarea>
<div class="row"><button id="saveDraftButton">Save draft</button><button class="primary" id="syncArticleButton">Sync article</button></div>
</div>
<div class="card"><h2>Upload Article File</h2>
<p class="muted">Use this for prepared article <code>.rsvp</code> files or short text files.</p>
<label>Article file</label><input id="articleFileInput" type="file" accept=".rsvp,.txt,.epub">
<p><button class="primary" id="uploadArticleButton">Upload article</button></p>
</div>
</div>
<div class="card"><h2>Articles</h2><div id="articlesList" class="muted">Loading...</div><p><button id="refreshArticlesButton">Refresh articles</button></p></div>
</section>

<section id="settings" class="page">
<div class="grid">
<div class="card"><h2>Word Pacing</h2>
<label>Pause behaviour</label><select id="pauseMode"><option value="sentence_end">End of sentence</option><option value="instant">Instant</option></select>
<label>Base speed <span id="wpmValue"></span></label><input id="wpm" type="range" min="10" max="1000" step="5">
<label>Long words <span id="longWordMsValue"></span></label><input id="longWordMs" type="range" min="0" max="600" step="50">
<label>Complexity <span id="complexWordMsValue"></span></label><input id="complexWordMs" type="range" min="0" max="600" step="50">
<label>Punctuation <span id="punctuationMsValue"></span></label><input id="punctuationMs" type="range" min="0" max="600" step="50">
</div>
<div class="card"><h2>Display</h2>
<label>Theme</label><select id="themeId"></select>
<label>Online theme</label><select id="onlineThemeId"></select>
<div class="row"><button id="installOnlineThemeButton">Install online theme</button></div>
<label>Theme file</label><input id="themeFileInput" type="file" accept=".rtheme">
<div class="row"><button id="uploadThemeButton">Upload theme file</button></div>
<hr>
<label>Online font</label><select id="onlineFontId"></select>
<label>Online font size</label><select id="onlineFontSize"><option value="large">Large</option><option value="medium">Medium</option><option value="small">Small</option></select>
<div class="row"><button id="installOnlineFontButton">Install online font size</button></div>
<label>Font family</label><input id="fontFamilyName" placeholder="Font folder name">
<label>Font size</label><select id="fontUploadSize"><option value="large">Large</option><option value="medium">Medium</option><option value="small">Small</option></select>
<label>Font file</label><input id="fontFileInput" type="file" accept=".rfont4">
<div class="row"><button id="uploadFontButton">Upload font file</button></div>
<label>Brightness <span id="brightnessValue"></span></label><input id="brightnessIndex" type="range" min="0" max="19">
<label>Reader hand</label><select id="handedness"><option value="right">Right</option><option value="left">Left</option></select>
<label>Footer label</label><select id="footerMetric"><option value="percentage">Percentage</option><option value="chapter_time">Chapter time</option><option value="book_time">Book time</option></select>
<label>Battery label</label><select id="batteryLabel"><option value="percent">Percentage</option><option value="time_remaining">Time remaining</option><option value="voltage">Voltage</option></select>
<label><input id="readingBattery" type="checkbox" style="width:auto"> Show battery while reading</label>
<label><input id="readingChapter" type="checkbox" style="width:auto"> Show chapter while reading</label>
<label><input id="readingProgress" type="checkbox" style="width:auto"> Show book percent while reading</label>
</div>
<div class="card"><h2>Typography</h2>
<label>Typeface</label><select id="typeface"><option value="literata">Literata</option></select>
<label>Font size <span id="fontSizeValue"></span></label><input id="fontSizeIndex" type="range" min="0" max="2">
<label>Tracking <span id="trackingValue"></span></label><input id="tracking" type="range" min="-2" max="3">
<label>Anchor <span id="anchorValue"></span></label><input id="anchorPercent" type="range" min="30" max="40">
<label>Guide width <span id="guideWidthValue"></span></label><input id="guideWidth" type="range" min="12" max="30" step="2">
<label>Guide gap <span id="guideGapValue"></span></label><input id="guideGap" type="range" min="2" max="8">
<label><input id="focusHighlight" type="checkbox" style="width:auto"> Focus highlight</label>
<label><input id="phantomWords" type="checkbox" style="width:auto"> Phantom words</label>
</div>
<div class="card"><h2>Home Wi-Fi</h2>
<p class="muted">Save Wi-Fi here for RSS and OTA. The reader does not send the saved password back to this page.</p>
<label>SSID</label><input id="wifiSsid" autocomplete="off" placeholder="Network name">
<label>Password</label><input id="wifiPassword" type="password" autocomplete="new-password" placeholder="Leave blank for open networks">
<div class="row"><button class="primary" id="saveWifiButton">Save Wi-Fi</button><button class="danger" id="forgetWifiButton">Forget</button></div>
<p id="wifiCurrent" class="muted">No saved Wi-Fi loaded yet.</p>
</div>
</div>
<p><button class="primary" id="saveSettingsButton">Save settings</button></p>
</section>

<section id="rss" class="page">
<div class="card"><h2>RSS Feeds</h2><p class="muted">Add one feed URL per line. Feeds are saved to <code>/config/rss.conf</code>; run RSS feeds from the reader menu to download articles.</p>
<textarea id="rssFeeds" placeholder="https://example.com/feed/"></textarea>
<p><button class="primary" id="saveRssButton">Save feeds</button> <button id="reloadRssButton">Reload</button></p>
</div>
</section>

<section id="help" class="page">
<div class="card"><h2>How to use this web companion</h2>
<ul>
<li>Open Companion sync on the reader, join the <code>RSVP-Nano</code> Wi-Fi network, then open this page.</li>
<li>Use Books for prepared book files and Articles for article drafts, article uploads, and synced articles.</li>
<li>For best book conversion, use the hosted web converter/flasher first. This page is the wireless upload and settings companion, not the full conversion engine.</li>
<li><code>.txt</code> and <code>.epub</code> uploads are accepted, but EPUB conversion is handled on the device when opened.</li>
<li>Use Wi-Fi to save your home network for RSS and OTA. You can still use the on-device Wi-Fi keyboard if you prefer the standalone path.</li>
<li>Use <code>/books/books</code> for books and <code>/books/articles</code> for articles. Files in <code>/books</code> still show up.</li>
</ul>
</div>
</section>
</main>
<script>
const $=id=>document.getElementById(id);let settings=null;let themeCatalog=[];let themeCatalogUrl='';let fontCatalog=[];let fontCatalogUrl='';
const THEME_CATALOG_URLS=['https://raw.githubusercontent.com/ionutdecebal/rsvpnano/main/themes/index.json','https://raw.githubusercontent.com/ReKylee/rsvpnano/main/themes/index.json'];
const FONT_CATALOG_URLS=['https://raw.githubusercontent.com/ionutdecebal/rsvpnano/main/src/fonts/index.json','https://raw.githubusercontent.com/ReKylee/rsvpnano/main/src/fonts/index.json'];
function status(msg){$('status').textContent=msg}
async function api(path,opts){const r=await fetch(path,opts);const t=await r.text();let j={};try{j=t?JSON.parse(t):{}}catch(e){throw new Error(t||'Bad response')}if(!r.ok)throw new Error((j.error&&j.error.message)||r.statusText);return j.data}
function bytes(n){return n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':(n/1048576).toFixed(1)+' MB'}
function safeName(s){return (s||'article').replace(/[^a-z0-9._ -]+/gi,'-').replace(/\s+/g,' ').trim().slice(0,72)||'article'}
function escRsvp(s){return (s||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n').trim()}
function articleFile(){const title=$('articleTitle').value.trim()||'Untitled Article';const author=$('articleAuthor').value.trim();const body=escRsvp($('articleBody').value);let out='@rsvp 1\n@title '+title+'\n';if(author)out+='@author '+author+'\n';out+='@para\n'+body+'\n';return {name:safeName(title)+'.rsvp',blob:new Blob([out],{type:'text/plain'})}}
function html(s){return String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function renderList(id,items){$(id).innerHTML=items.length?items.map(b=>`<div class="item"><div class="item-title">${html(b.metadata.title||b.name)}</div><div class="item-meta">${html([b.metadata.author,b.metadata.wordCount?b.metadata.wordCount+' words':null,b.metadata.chapterCount?b.metadata.chapterCount+' chapters':null,bytes(b.bytes),b.reading?b.reading.percent+'% read':null].filter(Boolean).join(' - '))}</div><p><button class="danger" data-delete="${html(encodeURIComponent(b.id))}">Delete</button></p></div>`).join(''):'<span class="muted">Nothing here yet.</span>';document.querySelectorAll('[data-delete]').forEach(b=>b.onclick=()=>delBook(decodeURIComponent(b.dataset.delete)))}
async function refresh(){try{const info=await api('/api/v1/device');$('infoBox').innerHTML=`${info.name}<br><span class="muted">${info.mode} - ${info.networkSsid||''}</span><br>Pairing code: <strong>${info.pairingCode}</strong>`;const data=await api('/api/v1/library');renderList('booksList',data.books.filter(b=>b.category!=='article'));renderList('articlesList',data.books.filter(b=>b.category==='article'));status('Connected to RSVP Nano.')}catch(e){status('Connection problem: '+e.message)}}
async function delBook(id){if(!confirm('Delete this item?'))return;try{await api('/api/v1/library?id='+encodeURIComponent(id),{method:'DELETE'});await refresh();status('Deleted')}catch(e){status('Delete failed: '+e.message)}}
async function uploadBlob(blob,name,category){const fd=new FormData();fd.append('file',blob,name);await api('/api/v1/library?name='+encodeURIComponent(name)+'&category='+encodeURIComponent(category),{method:'POST',body:fd})}
async function uploadPicked(inputId,category){const f=$(inputId).files[0];if(!f){status('Choose a file first.');return}try{await uploadBlob(f,f.name,category);$(inputId).value='';await refresh();status('Uploaded '+f.name)}catch(e){status('Upload failed: '+e.message)}}
async function uploadThemeBlob(blob,name){const fd=new FormData();fd.append('file',blob,name);await api('/api/v1/appearance/themes?name='+encodeURIComponent(name),{method:'POST',body:fd})}
async function uploadPickedTheme(){const f=$('themeFileInput').files[0];if(!f){status('Choose a theme file first.');return}try{await uploadThemeBlob(f,f.name);$('themeFileInput').value='';await loadSettings();status('Uploaded theme '+f.name)}catch(e){status('Theme upload failed: '+e.message)}}
async function loadThemeCatalog(){for(const url of THEME_CATALOG_URLS){try{themeCatalog=await fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Catalog unavailable');return r.json()});themeCatalogUrl=url;$('onlineThemeId').innerHTML=themeCatalog.map(t=>`<option value="${html(t.id)}">${html(t.name)}</option>`).join('');return}catch(e){}}$('onlineThemeId').innerHTML='<option value="">Catalog unavailable</option>'}
async function installOnlineTheme(){const id=val('onlineThemeId');const theme=themeCatalog.find(t=>t.id===id);if(!theme){status('Choose an online theme first.');return}try{const url=new URL(theme.file,themeCatalogUrl||THEME_CATALOG_URLS[0]).toString();const blob=await fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Theme unavailable');return r.blob()});await uploadThemeBlob(blob,theme.file);settings=await api('/api/v1/settings',{method:'PATCH',headers:{'Content-Type':'application/json'},body:JSON.stringify({display:{themeId:theme.id}})});await loadSettings();status('Installed '+theme.name)}catch(e){status('Online theme install failed: '+e.message)}}
function fontFamilyFromName(name){return safeName(String(name||'font').replace(/\.rfont4$/i,'').replace(/[-_ ]?(large|medium|small)$/i,''))||'font'}
async function uploadFontBlob(blob,family,size,name){const fd=new FormData();fd.append('file',blob,name||size+'.rfont4');await api('/api/v1/appearance/fonts?family='+encodeURIComponent(family)+'&size='+encodeURIComponent(size)+'&name='+encodeURIComponent(name||size+'.rfont4'),{method:'POST',body:fd})}
async function uploadPickedFont(){const f=$('fontFileInput').files[0];if(!f){status('Choose a font file first.');return}const family=$('fontFamilyName').value.trim()||fontFamilyFromName(f.name);const size=val('fontUploadSize');try{await uploadFontBlob(f,family,size,f.name);$('fontFileInput').value='';$('fontFamilyName').value='';await loadSettings();status('Uploaded '+family+' '+size)}catch(e){status('Font upload failed: '+e.message)}}
async function loadFontCatalog(){for(const url of FONT_CATALOG_URLS){try{fontCatalog=await fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Catalog unavailable');return r.json()});fontCatalogUrl=url;$('onlineFontId').innerHTML=fontCatalog.map(f=>`<option value="${html(f.id)}">${html(f.name)}</option>`).join('');return}catch(e){}}$('onlineFontId').innerHTML='<option value="">Catalog unavailable</option>'}
function onlineFontFile(font,size){if(font.files&&font.files[size])return font.files[size];return font.file||''}
async function installOnlineFont(){const id=val('onlineFontId');const size=val('onlineFontSize');const font=fontCatalog.find(f=>f.id===id);if(!font){status('Choose an online font first.');return}const file=onlineFontFile(font,size);if(!file){status('This online font is missing '+size+'.');return}try{const url=new URL(file,fontCatalogUrl||FONT_CATALOG_URLS[0]).toString();const blob=await fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Font unavailable');return r.blob()});await uploadFontBlob(blob,font.name||font.id,size,file.split('/').pop()||size+'.rfont4');await loadSettings();status('Installed '+(font.name||font.id)+' '+size)}catch(e){status('Online font install failed: '+e.message)}}
async function syncArticle(){const f=articleFile();if(!$('articleBody').value.trim()){status('Paste article text first.');return}try{await uploadBlob(f.blob,f.name,'article');localStorage.removeItem('rsvpArticleDraft');await refresh();status('Synced '+f.name)}catch(e){status('Article sync failed: '+e.message)}}
function saveDraft(){localStorage.setItem('rsvpArticleDraft',JSON.stringify({title:$('articleTitle').value,author:$('articleAuthor').value,body:$('articleBody').value}));status('Draft saved in this browser.')}
function loadDraft(){try{const d=JSON.parse(localStorage.getItem('rsvpArticleDraft')||'{}');$('articleTitle').value=d.title||'';$('articleAuthor').value=d.author||'';$('articleBody').value=d.body||''}catch(e){}}
function val(id){const e=$(id);return e.type==='checkbox'?e.checked:e.value}
function setVal(id,v){const e=$(id);if(e.type==='checkbox')e.checked=!!v;else e.value=v}
function setThemeOptions(){const themes=(settings&&settings.themes)||[];$('themeId').innerHTML=themes.map(t=>`<option value="${html(t.id)}">${html(t.name)}</option>`).join('')||'<option value="default">Default</option>'}
function setFontOptions(){const fonts=(settings&&settings.fonts)||[];$('typeface').innerHTML=fonts.map(f=>`<option value="${html(f.id)}">${html(f.name)}</option>`).join('')||'<option value="literata">Literata</option>'}
function selectThemeFont(){const theme=((settings&&settings.themes)||[]).find(t=>t.id===val('themeId'));if(theme)setVal('typeface',theme.typeface)}
function snapWpm(v){v=Math.max(10,Math.min(1000,Math.round(+v||300)));return v<=100?Math.max(10,Math.min(100,Math.round(v/10)*10)):Math.min(1000,100+Math.round((v-100)/25)*25)}
function updateLabels(){['wpm','longWordMs','complexWordMs','punctuationMs','brightnessIndex','fontSizeIndex','tracking','anchorPercent','guideWidth','guideGap'].forEach(id=>{const l=$(id+'Value')||$(id.replace('Index','')+'Value');if(l)l.textContent=id==='brightnessIndex'?(5+(+$(id).value*5))+'%':$(id).value+(id==='wpm'?' WPM':id.includes('Ms')?' ms':'')})}
async function loadSettings(){try{settings=await api('/api/v1/settings');setThemeOptions();setFontOptions();if(!themeCatalog.length)loadThemeCatalog();if(!fontCatalog.length)loadFontCatalog();setVal('pauseMode',settings.reading.pauseMode);setVal('wpm',snapWpm(settings.reading.wpm));setVal('longWordMs',settings.reading.pacing.longWordMs);setVal('complexWordMs',settings.reading.pacing.complexWordMs);setVal('punctuationMs',settings.reading.pacing.punctuationMs);setVal('themeId',settings.display.themeId||'default');setVal('brightnessIndex',settings.display.brightnessIndex);setVal('handedness',settings.display.handedness);setVal('footerMetric',settings.display.footerMetric);setVal('batteryLabel',settings.display.batteryLabel);setVal('readingBattery',settings.display.readingBattery);setVal('readingChapter',settings.display.readingChapter);setVal('readingProgress',settings.display.readingProgress);setVal('typeface',settings.typography.typeface);setVal('fontSizeIndex',settings.display.fontSizeIndex);setVal('tracking',settings.typography.tracking);setVal('anchorPercent',settings.typography.anchorPercent);setVal('guideWidth',settings.typography.guideWidth);setVal('guideGap',settings.typography.guideGap);setVal('focusHighlight',settings.typography.focusHighlight);setVal('phantomWords',settings.display.phantomWords);updateLabels()}catch(e){status('Settings load failed: '+e.message)}}
async function saveSettings(){setVal('wpm',snapWpm(val('wpm')));const payload={reading:{wpm:+val('wpm'),pauseMode:val('pauseMode'),pacing:{longWordMs:+val('longWordMs'),complexWordMs:+val('complexWordMs'),punctuationMs:+val('punctuationMs')}},display:{themeId:val('themeId'),brightnessIndex:+val('brightnessIndex'),handedness:val('handedness'),footerMetric:val('footerMetric'),batteryLabel:val('batteryLabel'),readingBattery:val('readingBattery'),readingChapter:val('readingChapter'),readingProgress:val('readingProgress'),phantomWords:val('phantomWords'),fontSizeIndex:+val('fontSizeIndex')},typography:{typeface:val('typeface'),focusHighlight:val('focusHighlight'),tracking:+val('tracking'),anchorPercent:+val('anchorPercent'),guideWidth:+val('guideWidth'),guideGap:+val('guideGap')}};try{settings=await api('/api/v1/settings',{method:'PATCH',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});status('Settings saved and applied.')}catch(e){status('Settings save failed: '+e.message)}}
async function loadWifi(){try{const w=await api('/api/v1/network');$('wifiSsid').value=w.ssid||'';$('wifiPassword').value='';$('wifiCurrent').textContent=w.configured?'Saved network: '+w.ssid:'No home Wi-Fi saved.'}catch(e){status('Wi-Fi load failed: '+e.message)}}
async function saveWifi(){const ssid=$('wifiSsid').value.trim();if(!ssid){status('Enter a Wi-Fi SSID first.');return}try{const w=await api('/api/v1/network',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password:$('wifiPassword').value})});$('wifiPassword').value='';$('wifiCurrent').textContent='Saved network: '+w.ssid;status('Wi-Fi saved for RSS and OTA.')}catch(e){status('Wi-Fi save failed: '+e.message)}}
async function forgetWifi(){if(!confirm('Forget saved Wi-Fi?'))return;try{await api('/api/v1/network',{method:'DELETE'});$('wifiSsid').value='';$('wifiPassword').value='';$('wifiCurrent').textContent='No home Wi-Fi saved.';status('Wi-Fi credentials cleared.')}catch(e){status('Forget Wi-Fi failed: '+e.message)}}
async function loadRss(){try{const r=await api('/api/v1/feeds');$('rssFeeds').value=(r.feeds||[]).join('\n');status('RSS feeds loaded.')}catch(e){status('RSS load failed: '+e.message)}}
async function saveRss(){const feeds=$('rssFeeds').value.split(/\n+/).map(s=>s.trim()).filter(Boolean);try{await api('/api/v1/feeds',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({feeds})});status('RSS feeds saved.')}catch(e){status('RSS save failed: '+e.message)}}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tabs button,.page').forEach(x=>x.classList.remove('active'));b.classList.add('active');$(b.dataset.tab).classList.add('active');if(b.dataset.tab==='settings'){loadSettings();loadWifi()}if(b.dataset.tab==='rss')loadRss()});
$('wpm').oninput=()=>{setVal('wpm',snapWpm(val('wpm')));updateLabels()};
['longWordMs','complexWordMs','punctuationMs','brightnessIndex','fontSizeIndex','tracking','anchorPercent','guideWidth','guideGap'].forEach(id=>$(id).oninput=updateLabels);
$('themeId').onchange=selectThemeFont;
$('refreshBooksButton').onclick=refresh;$('refreshArticlesButton').onclick=refresh;$('uploadBookButton').onclick=()=>uploadPicked('bookFileInput','book');$('uploadArticleButton').onclick=()=>uploadPicked('articleFileInput','article');$('uploadThemeButton').onclick=uploadPickedTheme;$('installOnlineThemeButton').onclick=installOnlineTheme;$('uploadFontButton').onclick=uploadPickedFont;$('installOnlineFontButton').onclick=installOnlineFont;$('syncArticleButton').onclick=syncArticle;$('saveDraftButton').onclick=saveDraft;$('saveSettingsButton').onclick=saveSettings;$('saveWifiButton').onclick=saveWifi;$('forgetWifiButton').onclick=forgetWifi;$('saveRssButton').onclick=saveRss;$('reloadRssButton').onclick=loadRss;
loadDraft();refresh();
</script>
</body>
</html>)HTML";

    bool isSafeFilenameChar(char c) {
        return AsciiText::isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == ' ';
    }

    String ipToString(IPAddress ip) {
        return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
    }

    String stripBom(String value) {
        if (value.length() >= 3 && static_cast<uint8_t>(value[0]) == 0xEF && static_cast<uint8_t>(value[1]) == 0xBB
            && static_cast<uint8_t>(value[2]) == 0xBF) {
            value.remove(0, 3);
        }
        return value;
    }

    bool directiveMatches(const String& loweredLine, const char* directive) {
        if (!loweredLine.startsWith(directive)) {
            return false;
        }
        const size_t directiveLength = strlen(directive);
        return loweredLine.length() == directiveLength || AsciiText::isWhitespace(loweredLine[directiveLength]);
    }

    String directiveValue(const String& line, const char* directive) {
        String value = line.substring(strlen(directive));
        value.trim();
        return value;
    }

    bool isSupportedBookName(const String& loweredName) {
        return loweredName.endsWith(".rsvp") || loweredName.endsWith(".txt") || loweredName.endsWith(".epub");
    }

    String displayNameForPath(const String& path) {
        const int separator = path.lastIndexOf('/');
        if (separator < 0) {
            return path;
        }
        return path.substring(separator + 1);
    }

    String relativeLibraryName(const String& path) {
        const String prefix = String(StoragePaths::kBooksPath) + "/";
        if (path.startsWith(prefix)) {
            return path.substring(prefix.length());
        }
        return displayNameForPath(path);
    }

    String libraryCategoryForPath(const String& path) {
        const String relative = relativeLibraryName(path);
        if (relative.startsWith("articles/")) {
            return "article";
        }
        if (relative.startsWith("books/")) {
            return "book";
        }
        return "root";
    }

    String enumLabel(uint8_t value, const char* const* labels, size_t count, uint8_t fallback = 0) {
        if (value >= count) {
            value = fallback;
        }
        return labels[value];
    }

    int enumValue(const String& value, const char* const* labels, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (value == labels[i]) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool findJsonKey(const String& body, const char* key, int& colonIndex) {
        const String needle = String("\"") + key + "\"";
        const int keyIndex = body.indexOf(needle);
        if (keyIndex < 0) {
            return false;
        }
        colonIndex = body.indexOf(':', keyIndex + needle.length());
        return colonIndex >= 0;
    }

    int skipJsonWhitespace(const String& body, int index) {
        while (index < static_cast<int>(body.length()) && AsciiText::isWhitespace(body[index])) {
            ++index;
        }
        return index;
    }

    bool readJsonInt(const String& body, const char* key, int& value) {
        int colonIndex = -1;
        if (!findJsonKey(body, key, colonIndex)) {
            return false;
        }
        int index = skipJsonWhitespace(body, colonIndex + 1);
        bool negative = false;
        if (index < static_cast<int>(body.length()) && body[index] == '-') {
            negative = true;
            ++index;
        }
        if (index >= static_cast<int>(body.length()) || !AsciiText::isDigit(body[index])) {
            return false;
        }
        int result = 0;
        while (index < static_cast<int>(body.length()) && AsciiText::isDigit(body[index])) {
            result = result * 10 + (body[index] - '0');
            ++index;
        }
        value = negative ? -result : result;
        return true;
    }

    bool readJsonUInt32(const String& body, const char* key, uint32_t& value) {
        int colonIndex = -1;
        if (!findJsonKey(body, key, colonIndex)) {
            return false;
        }
        int index = skipJsonWhitespace(body, colonIndex + 1);
        if (index >= static_cast<int>(body.length()) || !AsciiText::isDigit(body[index])) {
            return false;
        }

        uint32_t result = 0;
        while (index < static_cast<int>(body.length()) && AsciiText::isDigit(body[index])) {
            const uint32_t digit = static_cast<uint32_t>(body[index] - '0');
            if (result > (0xFFFFFFFFUL - digit) / 10UL) {
                return false;
            }
            result = result * 10UL + digit;
            ++index;
        }

        value = result;
        return true;
    }

    bool readJsonBool(const String& body, const char* key, bool& value) {
        int colonIndex = -1;
        if (!findJsonKey(body, key, colonIndex)) {
            return false;
        }
        const int index = skipJsonWhitespace(body, colonIndex + 1);
        if (body.substring(index, index + 4) == "true") {
            value = true;
            return true;
        }
        if (body.substring(index, index + 5) == "false") {
            value = false;
            return true;
        }
        return false;
    }

    bool readJsonString(const String& body, const char* key, String& value) {
        int colonIndex = -1;
        if (!findJsonKey(body, key, colonIndex)) {
            return false;
        }
        int index = skipJsonWhitespace(body, colonIndex + 1);
        if (index >= static_cast<int>(body.length()) || body[index] != '"') {
            return false;
        }
        ++index;
        String result;
        while (index < static_cast<int>(body.length())) {
            const char c = body[index++];
            if (c == '"') {
                value = result;
                return true;
            }
            if (c == '\\' && index < static_cast<int>(body.length())) {
                const char escaped = body[index++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    result += escaped;
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += escaped;
                    break;
                }
            } else {
                result += c;
            }
        }
        return false;
    }

    bool isHttpUrl(String value) {
        value.trim();
        value.toLowerCase();
        return value.startsWith("http://") || value.startsWith("https://");
    }

    bool nextJsonArrayString(const String& body, int& index, String& value) {
        index = skipJsonWhitespace(body, index);
        if (index >= static_cast<int>(body.length())) {
            return false;
        }
        if (body[index] == ',') {
            index = skipJsonWhitespace(body, index + 1);
        }
        if (index >= static_cast<int>(body.length()) || body[index] == ']') {
            return false;
        }
        if (body[index] != '"') {
            return false;
        }
        ++index;
        String result;
        while (index < static_cast<int>(body.length())) {
            const char c = body[index++];
            if (c == '"') {
                value = result;
                return true;
            }
            if (c == '\\' && index < static_cast<int>(body.length())) {
                const char escaped = body[index++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    result += escaped;
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                default:
                    result += escaped;
                    break;
                }
            } else {
                result += c;
            }
        }
        return false;
    }

    String rsvpMetadataValueFromLine(const String& line, const char* directive, bool& pastDirectives) {
        String trimmed = stripBom(line);
        trimmed.trim();
        if (trimmed.isEmpty()) {
            return "";
        }

        String lowered = trimmed;
        lowered.toLowerCase();
        if (directiveMatches(lowered, directive)) {
            return directiveValue(trimmed, directive);
        }

        if (!trimmed.startsWith("@")) {
            pastDirectives = true;
        }
        return "";
    }

} // namespace

CompanionSyncManager* CompanionSyncManager::instance_ = nullptr;

bool CompanionSyncManager::begin(Preferences& preferences) {
    if (active_) {
        return true;
    }

    instance_ = this;
    pairingCode_ = std::to_string(static_cast<uint32_t>(esp_random()) % 900000UL + 100000UL);
    statusLine1_ = "Starting sync";
    statusLine2_ = "Preparing Wi-Fi";
    preferences_ = &preferences;

    const bool networkReady = startAccessPoint();
    if (!networkReady) {
        statusLine1_ = "Wi-Fi failed";
        statusLine2_ = "";
        end();
        return false;
    }

    if (!startServer()) {
        statusLine1_ = "HTTP failed";
        statusLine2_ = "";
        end();
        return false;
    }

    active_ = true;
    statusLine1_ = networkSsid_;
    statusLine2_ = baseUrl();
    Serial.printf("[sync] ready ssid=%s url=%s pairing=%s\n", networkSsid_.c_str(), statusLine2_.c_str(),
                  pairingCode_.c_str());
    return true;
}

bool CompanionSyncManager::update() {
    if (!active_ || !serverStarted_) {
        return false;
    }
    server_.handleClient();
    const bool changed = settingsChanged_;
    settingsChanged_ = false;
    return changed;
}

void CompanionSyncManager::end() {
    stopServer();

    if (networkMode_ == NetworkMode::Station) {
        WiFi.disconnect(true, false);
    } else if (networkMode_ == NetworkMode::AccessPoint) {
        WiFi.softAPdisconnect(true);
    }
    WiFi.mode(WIFI_OFF);
    networkMode_ = NetworkMode::None;
    active_ = false;
    statusLine1_ = "Idle";
    statusLine2_ = "";
    preferences_ = nullptr;
    instance_ = nullptr;
}

bool CompanionSyncManager::active() const {
    return active_;
}

std::string_view CompanionSyncManager::statusLine1() const {
    return statusLine1_;
}

std::string_view CompanionSyncManager::statusLine2() const {
    return statusLine2_;
}

std::string CompanionSyncManager::baseUrl() const {
    if (networkMode_ == NetworkMode::Station) {
        return std::string{"http://"} + ipToString(WiFi.localIP()).c_str();
    }
    if (networkMode_ == NetworkMode::AccessPoint) {
        return std::string{"http://"} + ipToString(WiFi.softAPIP()).c_str();
    }
    return "";
}

void CompanionSyncManager::handleInfoStatic() {
    if (instance_ != nullptr) {
        instance_->handleInfo();
    }
}

void CompanionSyncManager::handleRootStatic() {
    if (instance_ != nullptr) {
        instance_->handleRoot();
    }
}

void CompanionSyncManager::handleBooksListStatic() {
    if (instance_ != nullptr) {
        instance_->handleBooksList();
    }
}

void CompanionSyncManager::handleSettingsStatic() {
    if (instance_ != nullptr) {
        instance_->handleSettings();
    }
}

void CompanionSyncManager::handleWifiStatic() {
    if (instance_ != nullptr) {
        instance_->handleWifi();
    }
}

void CompanionSyncManager::handleRssFeedsStatic() {
    if (instance_ != nullptr) {
        instance_->handleRssFeeds();
    }
}

void CompanionSyncManager::handleBookDeleteStatic() {
    if (instance_ != nullptr) {
        instance_->handleBookDelete();
    }
}

void CompanionSyncManager::handleBookPositionStatic() {
    if (instance_ != nullptr) {
        instance_->handleBookPosition();
    }
}

void CompanionSyncManager::handleBooksStatic() {
    if (instance_ != nullptr) {
        instance_->handleBooks();
    }
}

void CompanionSyncManager::handleBookUploadStatic() {
    if (instance_ != nullptr) {
        instance_->handleBookUpload();
    }
}

void CompanionSyncManager::handleThemesStatic() {
    if (instance_ != nullptr) {
        instance_->handleThemes();
    }
}

void CompanionSyncManager::handleThemeUploadStatic() {
    if (instance_ != nullptr) {
        instance_->handleThemeUpload();
    }
}

void CompanionSyncManager::handleFontsStatic() {
    if (instance_ != nullptr) {
        instance_->handleFonts();
    }
}

void CompanionSyncManager::handleFontUploadStatic() {
    if (instance_ != nullptr) {
        instance_->handleFontUpload();
    }
}

void CompanionSyncManager::handleNotFoundStatic() {
    if (instance_ != nullptr) {
        instance_->handleNotFound();
    }
}

bool CompanionSyncManager::startAccessPoint() {
    const std::string ssid = std::string{"RSVP-Nano-"} + deviceSuffix().c_str();
    statusLine1_ = "Sync Wi-Fi";
    statusLine2_ = ssid;
    networkSsid_ = ssid;
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid.c_str())) {
        Serial.println("[sync] softAP failed");
        return false;
    }

    networkMode_ = NetworkMode::AccessPoint;
    Serial.printf("[sync] softAP ssid=%s ip=%s\n", ssid.c_str(), ipToString(WiFi.softAPIP()).c_str());
    return true;
}

bool CompanionSyncManager::startServer() {
    server_.on("/", HTTP_GET, handleRootStatic);
    server_.on("/api/v1/device", HTTP_GET, handleInfoStatic);
    server_.on("/api/v1/library", HTTP_GET, handleBooksListStatic);
    server_.on("/api/v1/library", HTTP_DELETE, handleBookDeleteStatic);
    server_.on("/api/v1/library", HTTP_POST, handleBooksStatic, handleBookUploadStatic);
    server_.on("/api/v1/library/position", HTTP_PATCH, handleBookPositionStatic);
    server_.on("/api/v1/appearance/themes", HTTP_POST, handleThemesStatic, handleThemeUploadStatic);
    server_.on("/api/v1/appearance/fonts", HTTP_POST, handleFontsStatic, handleFontUploadStatic);
    server_.on("/api/v1/settings", HTTP_GET, handleSettingsStatic);
    server_.on("/api/v1/settings", HTTP_PATCH, handleSettingsStatic);
    server_.on("/api/v1/network", HTTP_GET, handleWifiStatic);
    server_.on("/api/v1/network", HTTP_PUT, handleWifiStatic);
    server_.on("/api/v1/network", HTTP_DELETE, handleWifiStatic);
    server_.on("/api/v1/feeds", HTTP_GET, handleRssFeedsStatic);
    server_.on("/api/v1/feeds", HTTP_PUT, handleRssFeedsStatic);
    server_.onNotFound(handleNotFoundStatic);
    server_.begin();
    serverStarted_ = true;

    if (networkMode_ == NetworkMode::Station && MDNS.begin(kMdnsName)) {
        MDNS.addService("http", "tcp", 80);
    }
    return true;
}

void CompanionSyncManager::stopServer() {
    if (serverStarted_) {
        server_.stop();
        MDNS.end();
    }
    finishUpload(false);
    serverStarted_ = false;
}

void CompanionSyncManager::handleInfo() {
    const String mode = networkMode_ == NetworkMode::Station ? "station" : "access_point";
    const String body = String("{") + "\"name\":\"RSVP Nano\"," + "\"mode\":\"" + mode + "\"," + "\"baseUrl\":\""
                      + jsonEscape(String(baseUrl().c_str())) + "\"," + "\"networkSsid\":\""
                      + jsonEscape(String(networkSsid_.c_str())) + "\"," + "\"pairingCode\":\"" + pairingCode_.c_str()
                      + "\"," + "\"apiVersion\":1,\"uploadPath\":\"/api/v1/library\"" + "}";
    sendData(200, body);
}

void CompanionSyncManager::handleRoot() {
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send_P(200, "text/html", kWebCompanionHtml);
}

void CompanionSyncManager::handleBooksList() {
    String body;
    body.reserve(1024);
    body += "{\"books\":[";
    bool first = true;
    const uint16_t wpm = settings::load<pref::Wpm>(*preferences_);

    const auto appendDirectory = [&](const char* directoryPath) {
        File dir = Board::Storage::filesystem().open(directoryPath);
        if (!dir || !dir.isDirectory()) {
            if (dir) {
                dir.close();
            }
            return;
        }

        File entry = dir.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) {
                const String name = displayNameForPath(String(entry.name()));
                const String path = String(directoryPath) + "/" + name;
                String lowered = name;
                lowered.toLowerCase();
                if (isSupportedBookName(lowered)) {
                    const RsvpMetadata sourceMetadata = readRsvpMetadata(path);
                    BookMetadata indexedMetadata;
                    IndexedBookStore::Header indexHeader;
                    const bool hasIndexedMetadata = IndexedBook::readMetadata(path, indexedMetadata, &indexHeader);
                    uint8_t progressPercent = 0;
                    uint32_t wordIndex = 0;
                    if (hasIndexedMetadata)
                        progressForPath(path, indexHeader.sourceSize, indexHeader.sourceFingerprint,
                                        indexHeader.wordCount, wordIndex, progressPercent);
                    if (!first) {
                        body += ",";
                    }
                    first = false;
                    body += "{\"id\":\"" + jsonEscape(bookIdForPath(path)) + "\",\"name\":\""
                          + jsonEscape(relativeLibraryName(path)) + "\",\"category\":\"" + libraryCategoryForPath(path)
                          + "\",\"bytes\":" + String(static_cast<uint32_t>(entry.size())) + ",\"metadata\":{"
                          + "\"title\":\""
                          + jsonEscape(hasIndexedMetadata && !indexedMetadata.title.empty()
                                           ? String(indexedMetadata.title.c_str())
                                           : sourceMetadata.title)
                          + "\",\"author\":\""
                          + jsonEscape(hasIndexedMetadata && !indexedMetadata.author.empty()
                                           ? String(indexedMetadata.author.c_str())
                                           : sourceMetadata.author)
                          + "\",\"wordCount\":" + String(hasIndexedMetadata ? indexHeader.wordCount : 0)
                          + ",\"chapterCount\":"
                          + String(hasIndexedMetadata ? static_cast<uint32_t>(indexedMetadata.chapters.size()) : 0)
                          + ",\"chapters\":[";
                    if (hasIndexedMetadata) {
                        for (size_t i = 0; i < indexedMetadata.chapters.size(); ++i) {
                            if (i > 0) {
                                body += ",";
                            }
                            body += "{\"title\":\"" + jsonEscape(indexedMetadata.chapters[i].title.c_str())
                                  + "\",\"wordIndex\":"
                                  + String(static_cast<uint32_t>(indexedMetadata.chapters[i].wordIndex)) + "}";
                        }
                    }
                    body += "]},\"source\":";
                    if (hasIndexedMetadata) {
                        body += "{\"size\":" + String(indexHeader.sourceSize)
                              + ",\"fingerprint\":" + String(indexHeader.sourceFingerprint) + "},\"reading\":{";
                        size_t chapterIndex = 0;
                        bool hasChapter = false;
                        for (size_t i = 0; i < indexedMetadata.chapters.size(); ++i) {
                            if (indexedMetadata.chapters[i].wordIndex > wordIndex)
                                break;
                            chapterIndex = i;
                            hasChapter = true;
                        }
                        const uint32_t remainingWords =
                            indexHeader.wordCount > wordIndex + 1 ? indexHeader.wordCount - wordIndex - 1 : 0;
                        body += "\"wordIndex\":" + String(wordIndex) + ",\"percent\":" + String(progressPercent)
                              + ",\"remainingWords\":" + String(remainingWords) + ",\"estimatedMinutes\":"
                              + String(wpm == 0 ? 0 : (remainingWords + wpm - 1) / wpm) + ",\"currentChapter\":";
                        if (hasChapter) {
                            body += "{\"number\":" + String(static_cast<uint32_t>(chapterIndex + 1)) + ",\"title\":\""
                                  + jsonEscape(indexedMetadata.chapters[chapterIndex].title.c_str()) + "\"}";
                        } else {
                            body += "null";
                        }
                        body += "}";
                    } else {
                        body += "null,\"reading\":null";
                    }
                    body += "}";
                }
            }
            entry.close();
            entry = dir.openNextFile();
        }

        dir.close();
    };

    appendDirectory(StoragePaths::kBooksPath);
    appendDirectory(StoragePaths::kBookFilesPath);
    appendDirectory(StoragePaths::kArticleFilesPath);

    body += "]}";
    sendData(200, body);
}

void CompanionSyncManager::handleSettings() {
    if (server_.method() == HTTP_GET) {
        sendData(200, settingsJson());
        return;
    }

    const String body = server_.arg("plain");
    if (body.length() > kMaxSettingsPatchBytes) {
        sendError(413, "payload_too_large", "Settings payload exceeds 8 KB");
        return;
    }

    String error;
    if (!applySettingsJson(body, error)) {
        sendError(422, "invalid_setting", error);
        return;
    }

    settingsChanged_ = true;
    sendData(200, settingsJson());
}

void CompanionSyncManager::handleWifi() {
    if (server_.method() == HTTP_GET) {
        sendData(200, wifiJson());
        return;
    }

    if (server_.method() == HTTP_DELETE) {
        settings::reset<pref::WifiSsid>(*preferences_);
        settings::reset<pref::WifiPassword>(*preferences_);
        statusLine1_ = "Wi-Fi cleared";
        statusLine2_ = "";
        sendData(200, wifiJson());
        return;
    }

    String error;
    if (!applyWifiJson(server_.arg("plain"), error)) {
        sendError(422, "invalid_network", error);
        return;
    }

    statusLine1_ = "Wi-Fi saved";
    statusLine2_ = settings::load<pref::WifiSsid>(*preferences_);
    sendData(200, wifiJson());
}

void CompanionSyncManager::handleRssFeeds() {
    if (server_.method() == HTTP_GET) {
        sendData(200, rssFeedsJson());
        return;
    }

    String error;
    if (!writeRssFeedsJson(server_.arg("plain"), error)) {
        sendError(422, "invalid_feed", error);
        return;
    }

    statusLine1_ = "RSS feeds saved";
    statusLine2_ = StoragePaths::kRssConfigPath;
    sendData(200, rssFeedsJson());
}

void CompanionSyncManager::handleBooks() {
    finishUpload(uploadError_.isEmpty());
    if (!uploadError_.isEmpty()) {
        sendError(422, "invalid_upload", uploadError_);
        uploadError_ = "";
        return;
    }

    sendData(201, String("{\"path\":\"") + jsonEscape(uploadFinalPath_) + "\"}");
    uploadFinalPath_ = "";
}

void CompanionSyncManager::handleThemes() {
    if (uploadFile_) {
        uploadFile_.close();
    }

    if (!uploadError_.isEmpty()) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        const int status = uploadError_.indexOf("already exists") >= 0 ? 409 : 400;
        sendError(status, status == 409 ? "already_exists" : "invalid_upload", uploadError_);
        uploadError_ = "";
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        return;
    }

    if (uploadTmpPath_.isEmpty() || uploadFinalPath_.isEmpty()) {
        sendError(400, "missing_upload", "Theme file is required", "file");
        return;
    }

    File tmpFile = Board::Storage::filesystem().open(uploadTmpPath_, FILE_READ);
    const size_t uploadSize = tmpFile ? static_cast<size_t>(tmpFile.size()) : 0;
    if (tmpFile) {
        tmpFile.close();
    }
    if (uploadSize == 0 || uploadSize > kMaxThemeUploadBytes) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_size", "Theme file must be between 1 byte and 4 KB", "file");
        return;
    }

    ui::themes::Theme theme;
    std::string error;
    const std::string id = ui::themes::themeIdFromPath({uploadFinalPath_.c_str(), uploadFinalPath_.length()});
    const String themeText = readSmallTextFile(uploadTmpPath_);
    if (!ui::themes::parseThemeText({themeText.c_str(), themeText.length()}, id, theme, error)) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_theme", error.c_str(), "file");
        return;
    }

    if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(409, "already_exists", "Theme already exists", "name");
        return;
    }

    if (!replaceUploadedFile(uploadTmpPath_, uploadFinalPath_)) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(500, "storage_error", "Theme could not be saved");
        return;
    }

    statusLine1_ = "Theme received";
    statusLine2_ = uploadFinalPath_.c_str();
    Serial.printf("[sync] theme ready %s\n", uploadFinalPath_.c_str());
    sendData(201, String("{\"path\":\"") + jsonEscape(uploadFinalPath_) + "\",\"id\":\""
                      + jsonEscape(String(id.c_str())) + "\"}");
    uploadTmpPath_ = "";
    uploadFinalPath_ = "";
}

void CompanionSyncManager::handleFonts() {
    if (uploadFile_) {
        uploadFile_.close();
    }

    if (!uploadError_.isEmpty()) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        const int status = uploadError_.indexOf("already exists") >= 0 ? 409 : 400;
        sendError(status, status == 409 ? "already_exists" : "invalid_upload", uploadError_);
        uploadError_ = "";
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        return;
    }

    if (uploadTmpPath_.isEmpty() || uploadFinalPath_.isEmpty()) {
        sendError(400, "missing_upload", "Font file is required", "file");
        return;
    }

    File tmpFile = Board::Storage::filesystem().open(uploadTmpPath_, FILE_READ);
    const size_t uploadSize = tmpFile ? static_cast<size_t>(tmpFile.size()) : 0;
    if (tmpFile) {
        tmpFile.close();
    }
    if (uploadSize == 0 || uploadSize > kMaxFontUploadBytes) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_size", "Font file must be between 1 byte and 2 MB", "file");
        return;
    }

    String error;
    if (!FontCatalog::validateFontFile(uploadTmpPath_, error)) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_font", error, "file");
        return;
    }

    if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(409, "already_exists", "Font size already exists", "size");
        return;
    }

    if (!replaceUploadedFile(uploadTmpPath_, uploadFinalPath_)) {
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(500, "storage_error", "Font could not be saved");
        return;
    }

    statusLine1_ = "Font received";
    statusLine2_ = uploadFinalPath_.c_str();
    Serial.printf("[sync] font ready %s\n", uploadFinalPath_.c_str());
    sendData(201, String("{\"path\":\"") + jsonEscape(uploadFinalPath_) + "\"}");
    uploadTmpPath_ = "";
    uploadFinalPath_ = "";
}

void CompanionSyncManager::handleFontUpload() {
    HTTPUpload& upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        String family = sanitizeFilename(server_.arg("family"));
        if (family.isEmpty()) {
            family = sanitizeFilename(upload.filename);
            const int dot = family.lastIndexOf('.');
            if (dot > 0) {
                family = family.substring(0, dot);
            }
        }
        if (family.isEmpty()) {
            uploadError_ = "Missing font family";
            return;
        }

        String sizeId = server_.arg("size");
        sizeId.toLowerCase();
        const size_t sizeIndex = RFont4::sizeIndexForId(sizeId.c_str());
        if (sizeIndex == RFont4::kSizeCount) {
            uploadError_ = "Font size must be large, medium, or small";
            return;
        }

        String filename = sanitizeFilename(server_.arg("name"));
        if (filename.isEmpty()) {
            filename = sanitizeFilename(upload.filename);
        }
        if (!RFont4::hasFontExtension(filename.c_str())) {
            filename += RFont4::kExtension;
        }
        if (!ensureFontFamilyDirectory(family)) {
            uploadError_ = "Fonts folder unavailable";
            return;
        }

        uploadFinalPath_ = String(StoragePaths::kFontsPath) + "/" + family + "/" + RFont4::sizeFilename(sizeIndex);
        if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
            uploadError_ = "Font size already exists";
            return;
        }
        uploadTmpPath_ = uploadFinalPath_ + ".tmp";
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadFile_ = Board::Storage::filesystem().open(uploadTmpPath_, FILE_WRITE);
        if (!uploadFile_) {
            uploadError_ = "Could not create file";
            return;
        }
        uploadError_ = "";
        statusLine1_ = "Receiving font";
        statusLine2_ = (family + " " + RFont4::sizeId(sizeIndex)).c_str();
        Serial.printf("[sync] font upload start %s\n", uploadFinalPath_.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!uploadError_.isEmpty() || !uploadFile_) {
            return;
        }
        if (static_cast<size_t>(upload.totalSize) + static_cast<size_t>(upload.currentSize) > kMaxFontUploadBytes) {
            uploadError_ = "Font file too large";
            uploadFile_.close();
            Board::Storage::filesystem().remove(uploadTmpPath_);
            return;
        }
        const size_t written = uploadFile_.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            uploadError_ = "Font write failed";
            uploadFile_.close();
            Board::Storage::filesystem().remove(uploadTmpPath_);
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile_) {
            uploadFile_.close();
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile_) {
            uploadFile_.close();
        }
        if (!uploadTmpPath_.isEmpty()) {
            Board::Storage::filesystem().remove(uploadTmpPath_);
        }
        uploadError_ = "Upload aborted";
        finishUpload(false);
    }
}

void CompanionSyncManager::handleBookDelete() {
    String path;
    const String id = server_.arg("id");
    if (id.isEmpty()) {
        sendError(400, "missing_field", "Book id is required", "id");
        return;
    }
    if (!resolveBookId(id, path)) {
        sendError(404, "book_not_found", "Book not found", "id");
        return;
    }

    if (!Board::Storage::filesystem().remove(path)) {
        sendError(500, "storage_error", "Book could not be deleted");
        return;
    }

    statusLine1_ = "Book deleted";
    statusLine2_ = relativeLibraryName(path).c_str();
    Serial.printf("[sync] deleted %s\n", path.c_str());
    sendData(200, String("{\"id\":\"") + jsonEscape(id) + "\",\"deleted\":true}");
}

void CompanionSyncManager::handleBookPosition() {
    const String body = server_.arg("plain");
    if (body.length() > 512) {
        sendError(413, "payload_too_large", "Position payload exceeds 512 bytes");
        return;
    }

    String id;
    uint32_t requestedWordIndex = 0;
    if (!readJsonString(body, "id", id) || !readJsonUInt32(body, "wordIndex", requestedWordIndex)) {
        sendError(400, "missing_field", "Book id and wordIndex are required");
        return;
    }

    String path;
    if (!resolveBookId(id, path)) {
        sendError(404, "book_not_found", "Book not found", "id");
        return;
    }

    BookMetadata metadata;
    IndexedBookStore::Header header;
    if (!IndexedBook::readMetadata(path, metadata, &header)) {
        sendError(409, "index_unavailable", "Book must be indexed on the reader before changing position");
        return;
    }
    if (header.wordCount == 0) {
        sendError(409, "empty_book", "Book has no readable words");
        return;
    }

    const uint32_t wordIndex = std::min<uint32_t>(requestedWordIndex, header.wordCount - 1);
    if (!ReadingProgress::writePositionSidecar(path, {header.sourceSize, header.sourceFingerprint, header.wordCount},
                                               wordIndex)) {
        sendError(500, "storage_error", "Reading position could not be saved");
        return;
    }

    ReadingProgress::cachePosition(*preferences_, path, {header.sourceSize, header.sourceFingerprint, header.wordCount},
                                   wordIndex);
    statusLine1_ = "Position saved";
    statusLine2_ = relativeLibraryName(path).c_str();
    sendData(200, String("{\"id\":\"") + jsonEscape(id) + "\",\"wordIndex\":" + String(wordIndex)
                      + ",\"percent\":" + String(ReadingProgress::percent(wordIndex, header.wordCount)) + "}");
}

void CompanionSyncManager::handleBookUpload() {
    HTTPUpload& upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        String filename = sanitizeFilename(server_.arg("name"));
        if (filename.isEmpty()) {
            filename = sanitizeFilename(upload.filename);
        }
        if (filename.isEmpty()) {
            uploadError_ = "Missing filename";
            return;
        }

        String lowered = filename;
        lowered.toLowerCase();
        if (!isSupportedBookName(lowered)) {
            filename += ".rsvp";
        }

        String category = server_.arg("category");
        category.toLowerCase();
        const char* targetDirectory =
            category == "article" ? StoragePaths::kArticleFilesPath : StoragePaths::kBookFilesPath;

        if (!ensureLibraryDirectories()) {
            uploadError_ = "Library folders unavailable";
            return;
        }
        uploadFinalPath_ = String(targetDirectory) + "/" + filename;
        uploadTmpPath_ = uploadFinalPath_ + ".tmp";
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadFile_ = Board::Storage::filesystem().open(uploadTmpPath_, FILE_WRITE);
        if (!uploadFile_) {
            uploadError_ = "Could not create file";
            return;
        }
        uploadError_ = "";
        statusLine1_ = "Receiving book";
        statusLine2_ = filename.c_str();
        Serial.printf("[sync] upload start %s\n", uploadFinalPath_.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!uploadError_.isEmpty() || !uploadFile_) {
            return;
        }
        const size_t written = uploadFile_.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            uploadError_ = "Write failed";
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("[sync] upload end bytes=%u error=%s\n", upload.totalSize, uploadError_.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        uploadError_ = "Upload aborted";
        finishUpload(false);
    }
}

void CompanionSyncManager::handleThemeUpload() {
    HTTPUpload& upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        String filename = sanitizeFilename(server_.arg("name"));
        if (filename.isEmpty()) {
            filename = sanitizeFilename(upload.filename);
        }
        if (filename.isEmpty()) {
            uploadError_ = "Missing filename";
            return;
        }
        if (!ui::themes::hasThemeExtension({filename.c_str(), filename.length()})) {
            filename += ui::themes::kThemeExtension.data();
        }
        if (!ensureThemeDirectory()) {
            uploadError_ = "Themes folder unavailable";
            return;
        }

        uploadFinalPath_ = String(StoragePaths::kThemesPath) + "/" + filename;
        if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
            uploadError_ = "Theme already exists";
            return;
        }
        uploadTmpPath_ = uploadFinalPath_ + ".tmp";
        Board::Storage::filesystem().remove(uploadTmpPath_);
        uploadFile_ = Board::Storage::filesystem().open(uploadTmpPath_, FILE_WRITE);
        if (!uploadFile_) {
            uploadError_ = "Could not create file";
            return;
        }
        uploadError_ = "";
        statusLine1_ = "Receiving theme";
        statusLine2_ = filename.c_str();
        Serial.printf("[sync] theme upload start %s\n", uploadFinalPath_.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!uploadError_.isEmpty() || !uploadFile_) {
            return;
        }
        if (upload.totalSize + upload.currentSize > kMaxThemeUploadBytes) {
            uploadError_ = "Theme too large";
            return;
        }
        const size_t written = uploadFile_.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            uploadError_ = "Write failed";
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        uploadError_ = "Upload aborted";
        finishUpload(false);
    }
}

void CompanionSyncManager::handleNotFound() {
    sendError(404, "not_found", "API route not found");
}

void CompanionSyncManager::sendData(int status, const String& json) {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(status, "application/json", String("{\"data\":") + json + "}");
}

void CompanionSyncManager::sendError(int status, const char* code, const String& message, const char* field) {
    String body =
        String("{\"error\":{\"code\":\"") + jsonEscape(code) + "\",\"message\":\"" + jsonEscape(message) + "\"";
    if (field != nullptr && *field != '\0')
        body += String(",\"field\":\"") + jsonEscape(field) + "\"";
    body += "}}";
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(status, "application/json", body);
}

String CompanionSyncManager::settingsJson() {
    static const char* const handednessLabels[] = {"right", "left"};
    static const char* const footerMetricLabels[] = {"percentage", "chapter_time", "book_time"};
    static const char* const batteryLabelLabels[] = {"percent", "time_remaining", "voltage"};
    static const char* const pauseModeLabels[] = {"sentence_end", "instant"};

    const uint16_t wpm = settings::load<pref::Wpm>(*preferences_);
    const ::PauseMode pauseMode = settings::load<pref::PauseMode>(*preferences_);
    const uint16_t longDelay = settings::load<pref::PacingLongWordDelay>(*preferences_);
    const uint16_t complexDelay = settings::load<pref::PacingComplexWordDelay>(*preferences_);
    const uint16_t punctuationDelay = settings::load<pref::PacingPunctuationDelay>(*preferences_);
    const uint8_t brightness = settings::load<pref::BrightnessIndex>(*preferences_);
    const bool handedness = settings::load<pref::Handedness>(*preferences_);
    const FooterMetric footerMetric = settings::load<pref::FooterMetricMode>(*preferences_);
    const BatteryLabel batteryLabel = settings::load<pref::BatteryLabelMode>(*preferences_);
    const UiLanguage language = settings::load<pref::UiLanguage>(*preferences_);
    const uint8_t fontSize = settings::load<pref::ReaderFontSizeIndex>(*preferences_);
    FontCatalog fontCatalog;
    fontCatalog.loadFromSd();
    const int tracking = settings::load<pref::TypographyTracking>(*preferences_);
    const uint8_t anchor = settings::load<pref::TypographyAnchor>(*preferences_);
    const uint8_t guideWidth = settings::load<pref::TypographyGuideWidth>(*preferences_);
    const uint8_t guideGap = settings::load<pref::TypographyGuideGap>(*preferences_);
    ThemeStore themeStore;
    themeStore.loadFromSd(fontCatalog);
    const std::string savedThemeId = settings::load<pref::ThemeId>(*preferences_);
    if (!savedThemeId.empty()) {
        themeStore.selectById(savedThemeId);
    }
    const ui::themes::Theme& selectedTheme = themeStore.selected();

    String body;
    body.reserve(3600);
    body += "{\"version\":1,\"applied\":true";
    body += ",\"reading\":{";
    body += "\"wpm\":" + String(wpm);
    body += ",\"pauseMode\":\"";
    body += enumLabel(static_cast<uint8_t>(pauseMode), pauseModeLabels, 2);
    body += "\"";
    body += ",\"pacing\":{\"longWordMs\":" + String(longDelay) + ",\"complexWordMs\":" + String(complexDelay)
          + ",\"punctuationMs\":" + String(punctuationDelay) + "}";
    body += "}";
    body += ",\"display\":{";
    body += "\"themeId\":\"" + jsonEscape(String(selectedTheme.id.c_str())) + "\"";
    body += ",\"brightnessIndex\":" + String(brightness);
    body += ",\"handedness\":\"";
    body += enumLabel(static_cast<uint8_t>(handedness), handednessLabels, 2);
    body += "\"";
    body += ",\"footerMetric\":\"";
    body += enumLabel(static_cast<uint8_t>(footerMetric), footerMetricLabels, 3);
    body += "\"";
    body += ",\"batteryLabel\":\"";
    body += enumLabel(static_cast<uint8_t>(batteryLabel), batteryLabelLabels, 3);
    body += "\"";
    body +=
        ",\"readingBattery\":" + String(settings::load<pref::ReaderBatteryVisible>(*preferences_) ? "true" : "false");
    body +=
        ",\"readingChapter\":" + String(settings::load<pref::ReaderChapterVisible>(*preferences_) ? "true" : "false");
    body +=
        ",\"readingProgress\":" + String(settings::load<pref::ReaderProgressVisible>(*preferences_) ? "true" : "false");
    body += ",\"language\":" + String(static_cast<uint8_t>(language));
    body += ",\"screensaver\":" + String(static_cast<uint8_t>(settings::load<pref::ScreensaverMode>(*preferences_)));
    body += ",\"standbyTimerIndex\":" + String(settings::load<pref::StandbyTimerIndex>(*preferences_));
    body += ",\"phantomWords\":" + String(settings::load<pref::PhantomWords>(*preferences_) ? "true" : "false");
    body += ",\"fontSizeIndex\":" + String(fontSize);
    body += "}";
    body += ",\"typography\":{";
    body += "\"typeface\":\"";
    body += jsonEscape(String(selectedTheme.typeface.c_str()));
    body += "\"";
    body += ",\"focusHighlight\":"
          + String(settings::load<pref::TypographyFocusHighlight>(*preferences_) ? "true" : "false");
    body += ",\"tracking\":" + String(tracking);
    body += ",\"anchorPercent\":" + String(anchor);
    body += ",\"guideWidth\":" + String(guideWidth);
    body += ",\"guideGap\":" + String(guideGap);
    body += "}";
    const auto themes = themeStore.themes();
    body += ",\"themeCount\":" + String(themes.size());
    body += ",\"themes\":[";
    for (size_t i = 0; i < themes.size(); ++i) {
        if (i > 0) {
            body += ",";
        }
        body += "{\"id\":\"" + jsonEscape(String(themes[i].id.c_str())) + "\"";
        body += ",\"name\":\"" + jsonEscape(String(themes[i].name.c_str())) + "\"";
        body += ",\"builtIn\":" + String(themes[i].builtIn ? "true" : "false");
        body += ",\"typeface\":\"" + jsonEscape(String(themes[i].typeface.c_str())) + "\"}";
    }
    body += "]";
    const auto fonts = fontCatalog.families();
    body += ",\"fontCount\":" + String(fonts.size());
    body += ",\"fonts\":[";
    for (size_t i = 0; i < fonts.size(); ++i) {
        if (i > 0) {
            body += ",";
        }
        body += "{\"id\":\"" + jsonEscape(String(fonts[i].id.c_str())) + "\"";
        body += ",\"name\":\"" + jsonEscape(String(fonts[i].label.c_str())) + "\"";
        body += ",\"builtIn\":" + String(fonts[i].builtIn ? "true" : "false");
        body += ",\"sizes\":[";
        for (size_t size = 0; size < RFont4::kSizeCount; ++size) {
            if (size > 0) {
                body += ",";
            }
            body += "{\"id\":\"";
            body += RFont4::sizeId(size);
            body += "\",\"name\":\"";
            body += RFont4::sizeLabel(size);
            body += "\",\"available\":";
            const bool available = fonts[i].builtIn || !fonts[i].paths[size].empty();
            body += available ? "true" : "false";
            body += "}";
        }
        body += "]}";
    }
    body += "]";
    body += ",\"limits\":{";
    body += "\"wpm\":{\"min\":" + String(pref::Wpm::minValue()) + ",\"max\":" + String(pref::Wpm::maxValue()) + "}";
    body += ",\"brightnessIndex\":{\"min\":0,\"max\":" + String(kBrightnessCount - 1) + "}";
    body += ",\"pacingMs\":{\"min\":" + String(pref::PacingLongWordDelay::minValue())
          + ",\"max\":" + String(pref::PacingLongWordDelay::maxValue()) + "}";
    body += ",\"tracking\":{\"min\":" + String(pref::TypographyTracking::minValue())
          + ",\"max\":" + String(pref::TypographyTracking::maxValue()) + "}";
    body += ",\"anchorPercent\":{\"min\":" + String(pref::TypographyAnchor::minValue())
          + ",\"max\":" + String(pref::TypographyAnchor::maxValue()) + "}";
    body += ",\"guideWidth\":{\"min\":" + String(pref::TypographyGuideWidth::minValue())
          + ",\"max\":" + String(pref::TypographyGuideWidth::maxValue()) + "}";
    body += ",\"guideGap\":{\"min\":" + String(pref::TypographyGuideGap::minValue())
          + ",\"max\":" + String(pref::TypographyGuideGap::maxValue()) + "}";
    body += "}}";
    return body;
}

bool CompanionSyncManager::applySettingsJson(const String& body, String& error) {
    if (body.isEmpty()) {
        error = "Missing settings JSON";
        return false;
    }

    static const char* const handednessLabels[] = {"right", "left"};
    static const char* const footerMetricLabels[] = {"percentage", "chapter_time", "book_time"};
    static const char* const batteryLabelLabels[] = {"percent", "time_remaining", "voltage"};
    static const char* const pauseModeLabels[] = {"sentence_end", "instant"};

    struct Values {
        uint16_t wpm;
        ::PauseMode pauseMode;
        uint16_t longWordMs;
        uint16_t complexWordMs;
        uint16_t punctuationMs;
        uint8_t brightnessIndex;
        std::string themeId;
        bool leftHanded;
        FooterMetric footerMetric;
        BatteryLabel batteryLabel;
        bool readingBattery;
        bool readingChapter;
        bool readingProgress;
        UiLanguage language;
        standby::Kind screensaver;
        uint8_t standbyTimerIndex;
        bool phantomWords;
        uint8_t fontSizeIndex;
        std::string typefaceId;
        bool focusHighlight;
        int8_t tracking;
        uint8_t anchorPercent;
        uint8_t guideWidth;
        uint8_t guideGap;
    };

    FontCatalog fontCatalog;
    fontCatalog.loadFromSd();
    ThemeStore themeStore;
    themeStore.loadFromSd(fontCatalog);
    const std::string savedThemeId = settings::load<pref::ThemeId>(*preferences_);
    if (!savedThemeId.empty())
        themeStore.selectById(savedThemeId);
    const ui::themes::Theme& currentTheme = themeStore.selected();

    const Values current{
        settings::load<pref::Wpm>(*preferences_),
        settings::load<pref::PauseMode>(*preferences_),
        settings::load<pref::PacingLongWordDelay>(*preferences_),
        settings::load<pref::PacingComplexWordDelay>(*preferences_),
        settings::load<pref::PacingPunctuationDelay>(*preferences_),
        settings::load<pref::BrightnessIndex>(*preferences_),
        currentTheme.id,
        settings::load<pref::Handedness>(*preferences_),
        settings::load<pref::FooterMetricMode>(*preferences_),
        settings::load<pref::BatteryLabelMode>(*preferences_),
        settings::load<pref::ReaderBatteryVisible>(*preferences_),
        settings::load<pref::ReaderChapterVisible>(*preferences_),
        settings::load<pref::ReaderProgressVisible>(*preferences_),
        settings::load<pref::UiLanguage>(*preferences_),
        settings::load<pref::ScreensaverMode>(*preferences_),
        settings::load<pref::StandbyTimerIndex>(*preferences_),
        settings::load<pref::PhantomWords>(*preferences_),
        settings::load<pref::ReaderFontSizeIndex>(*preferences_),
        currentTheme.typeface,
        settings::load<pref::TypographyFocusHighlight>(*preferences_),
        settings::load<pref::TypographyTracking>(*preferences_),
        settings::load<pref::TypographyAnchor>(*preferences_),
        settings::load<pref::TypographyGuideWidth>(*preferences_),
        settings::load<pref::TypographyGuideGap>(*preferences_),
    };
    Values next = current;
    int intValue = 0;
    bool boolValue = false;
    String stringValue;

    if (readJsonInt(body, "wpm", intValue)) {
        if (intValue < pref::Wpm::minValue() || intValue > pref::Wpm::maxValue()) {
            error = "wpm must be between 10 and 1000";
            return false;
        }
        next.wpm = static_cast<uint16_t>(intValue);
    }
    if (readJsonString(body, "pauseMode", stringValue)) {
        const int value = enumValue(stringValue, pauseModeLabels, 2);
        if (value < 0) {
            error = "pauseMode must be sentence_end or instant";
            return false;
        }
        next.pauseMode = static_cast<::PauseMode>(value);
    }
    if (readJsonInt(body, "longWordMs", intValue)) {
        if (intValue < pref::PacingLongWordDelay::minValue() || intValue > pref::PacingLongWordDelay::maxValue()) {
            error = "longWordMs must be between 0 and 600";
            return false;
        }
        next.longWordMs = static_cast<uint16_t>(intValue);
    }
    if (readJsonInt(body, "complexWordMs", intValue)) {
        if (intValue < pref::PacingComplexWordDelay::minValue()
            || intValue > pref::PacingComplexWordDelay::maxValue()) {
            error = "complexWordMs must be between 0 and 600";
            return false;
        }
        next.complexWordMs = static_cast<uint16_t>(intValue);
    }
    if (readJsonInt(body, "punctuationMs", intValue)) {
        if (intValue < pref::PacingPunctuationDelay::minValue()
            || intValue > pref::PacingPunctuationDelay::maxValue()) {
            error = "punctuationMs must be between 0 and 600";
            return false;
        }
        next.punctuationMs = static_cast<uint16_t>(intValue);
    }
    if (readJsonInt(body, "brightnessIndex", intValue)) {
        if (intValue < 0 || intValue >= static_cast<int>(kBrightnessCount)) {
            error = "brightnessIndex must be between 0 and 19";
            return false;
        }
        next.brightnessIndex = static_cast<uint8_t>(intValue);
    }
    if (readJsonString(body, "themeId", stringValue)) {
        if (!themeStore.selectById({stringValue.c_str(), stringValue.length()})) {
            error = "themeId does not match an available theme";
            return false;
        }
        const ui::themes::Theme& theme = themeStore.selected();
        next.themeId = theme.id;
        next.typefaceId = theme.typeface;
    }
    if (readJsonString(body, "handedness", stringValue)) {
        const int value = enumValue(stringValue, handednessLabels, 2);
        if (value < 0) {
            error = "handedness must be right or left";
            return false;
        }
        next.leftHanded = value != 0;
    }
    if (readJsonString(body, "footerMetric", stringValue)) {
        const int value = enumValue(stringValue, footerMetricLabels, 3);
        if (value < 0) {
            error = "footerMetric must be percentage, chapter_time, or book_time";
            return false;
        }
        next.footerMetric = static_cast<FooterMetric>(value);
    }
    if (readJsonString(body, "batteryLabel", stringValue)) {
        const int value = enumValue(stringValue, batteryLabelLabels, 3);
        if (value < 0) {
            error = "batteryLabel must be percent, time_remaining, or voltage";
            return false;
        }
        next.batteryLabel = static_cast<BatteryLabel>(value);
    }
    if (readJsonBool(body, "readingBattery", boolValue)) {
        next.readingBattery = boolValue;
    }
    if (readJsonBool(body, "readingChapter", boolValue)) {
        next.readingChapter = boolValue;
    }
    if (readJsonBool(body, "readingProgress", boolValue)) {
        next.readingProgress = boolValue;
    }
    if (readJsonInt(body, "language", intValue)) {
        if (intValue < 0 || intValue >= static_cast<int>(pref::UiLanguage::count())) {
            error = "language is out of range";
            return false;
        }
        next.language = static_cast<UiLanguage>(intValue);
    }
    if (readJsonInt(body, "screensaver", intValue)) {
        if (intValue < 0 || intValue >= static_cast<int>(standby::Kind::Count)) {
            error = "screensaver is out of range";
            return false;
        }
        next.screensaver = static_cast<standby::Kind>(intValue);
    }
    if (readJsonInt(body, "standbyTimerIndex", intValue)) {
        if (intValue < pref::StandbyTimerIndex::minValue() || intValue > pref::StandbyTimerIndex::maxValue()) {
            error = "standbyTimerIndex is out of range";
            return false;
        }
        next.standbyTimerIndex = static_cast<uint8_t>(intValue);
    }
    if (readJsonBool(body, "phantomWords", boolValue)) {
        next.phantomWords = boolValue;
    }
    if (readJsonInt(body, "fontSizeIndex", intValue)) {
        if (intValue < 0 || intValue >= RFont4::kSizeCount) {
            error = "fontSizeIndex must be between 0 and 2";
            return false;
        }
        next.fontSizeIndex = static_cast<uint8_t>(intValue);
    }
    if (readJsonString(body, "typeface", stringValue)) {
        const FontCatalog::Family* family = fontCatalog.find(stringValue.c_str());
        if (family == nullptr) {
            error = "typeface does not match an available font";
            return false;
        }
        next.typefaceId = family->id;
    }
    if (readJsonBool(body, "focusHighlight", boolValue)) {
        next.focusHighlight = boolValue;
    }
    if (readJsonInt(body, "tracking", intValue)) {
        if (intValue < pref::TypographyTracking::minValue() || intValue > pref::TypographyTracking::maxValue()) {
            error = "tracking is out of range";
            return false;
        }
        next.tracking = static_cast<int8_t>(intValue);
    }
    if (readJsonInt(body, "anchorPercent", intValue)) {
        if (intValue < pref::TypographyAnchor::minValue() || intValue > pref::TypographyAnchor::maxValue()) {
            error = "anchorPercent is out of range";
            return false;
        }
        next.anchorPercent = static_cast<uint8_t>(intValue);
    }
    if (readJsonInt(body, "guideWidth", intValue)) {
        if (intValue < pref::TypographyGuideWidth::minValue() || intValue > pref::TypographyGuideWidth::maxValue()) {
            error = "guideWidth is out of range";
            return false;
        }
        next.guideWidth = static_cast<uint8_t>(intValue);
    }
    if (readJsonInt(body, "guideGap", intValue)) {
        if (intValue < pref::TypographyGuideGap::minValue() || intValue > pref::TypographyGuideGap::maxValue()) {
            error = "guideGap is out of range";
            return false;
        }
        next.guideGap = static_cast<uint8_t>(intValue);
    }

    const auto save = [&](const Values& values) {
        bool ok = true;
        ok = settings::save<pref::Wpm>(*preferences_, values.wpm) && ok;
        ok = settings::save<pref::PauseMode>(*preferences_, values.pauseMode) && ok;
        ok = settings::save<pref::PacingLongWordDelay>(*preferences_, values.longWordMs) && ok;
        ok = settings::save<pref::PacingComplexWordDelay>(*preferences_, values.complexWordMs) && ok;
        ok = settings::save<pref::PacingPunctuationDelay>(*preferences_, values.punctuationMs) && ok;
        ok = settings::save<pref::BrightnessIndex>(*preferences_, values.brightnessIndex) && ok;
        ok = settings::save<pref::ThemeId>(*preferences_, values.themeId) && ok;
        ok = settings::save<pref::Handedness>(*preferences_, values.leftHanded) && ok;
        ok = settings::save<pref::FooterMetricMode>(*preferences_, values.footerMetric) && ok;
        ok = settings::save<pref::BatteryLabelMode>(*preferences_, values.batteryLabel) && ok;
        ok = settings::save<pref::ReaderBatteryVisible>(*preferences_, values.readingBattery) && ok;
        ok = settings::save<pref::ReaderChapterVisible>(*preferences_, values.readingChapter) && ok;
        ok = settings::save<pref::ReaderProgressVisible>(*preferences_, values.readingProgress) && ok;
        ok = settings::save<pref::UiLanguage>(*preferences_, values.language) && ok;
        ok = settings::save<pref::ScreensaverMode>(*preferences_, values.screensaver) && ok;
        ok = settings::save<pref::StandbyTimerIndex>(*preferences_, values.standbyTimerIndex) && ok;
        ok = settings::save<pref::PhantomWords>(*preferences_, values.phantomWords) && ok;
        ok = settings::save<pref::ReaderFontSizeIndex>(*preferences_, values.fontSizeIndex) && ok;
        ok = settings::save<pref::TypographyFocusHighlight>(*preferences_, values.focusHighlight) && ok;
        ok = settings::save<pref::TypographyTracking>(*preferences_, values.tracking) && ok;
        ok = settings::save<pref::TypographyAnchor>(*preferences_, values.anchorPercent) && ok;
        ok = settings::save<pref::TypographyGuideWidth>(*preferences_, values.guideWidth) && ok;
        ok = settings::save<pref::TypographyGuideGap>(*preferences_, values.guideGap) && ok;
        return ok;
    };

    if (!themeStore.selectById(next.themeId)) {
        error = "themeId does not match an available theme";
        return false;
    }
    const std::string previousTypeface = themeStore.selected().typeface;
    if (!themeStore.setSelectedTypeface(next.typefaceId, fontCatalog)) {
        error = "Theme typeface could not be saved";
        return false;
    }
    if (save(next))
        return true;

    themeStore.setSelectedTypeface(previousTypeface, fontCatalog);
    save(current);
    error = "Settings could not be saved";
    return false;
}

String CompanionSyncManager::wifiJson() {
    const std::string ssid = settings::load<pref::WifiSsid>(*preferences_);
    return String("{\"configured\":") + (ssid.empty() ? "false" : "true") + ",\"ssid\":\"" + jsonEscape(ssid.c_str())
         + "\",\"passwordSet\":" + (settings::load<pref::WifiPassword>(*preferences_).empty() ? "false" : "true") + "}";
}

bool CompanionSyncManager::applyWifiJson(const String& body, String& error) {
    if (body.length() > 512) {
        error = "Wi-Fi payload too large";
        return false;
    }

    String ssid;
    if (!readJsonString(body, "ssid", ssid)) {
        error = "Missing Wi-Fi SSID";
        return false;
    }
    ssid.trim();
    if (ssid.isEmpty()) {
        error = "Wi-Fi SSID is required";
        return false;
    }
    if (ssid.length() > 32) {
        error = "Wi-Fi SSID is too long";
        return false;
    }

    String password;
    readJsonString(body, "password", password);
    if (password.length() > 64) {
        error = "Wi-Fi password is too long";
        return false;
    }

    settings::save<pref::WifiSsid>(*preferences_, ssid.c_str());
    settings::save<pref::WifiPassword>(*preferences_, password.c_str());
    return true;
}

String CompanionSyncManager::rssFeedsJson() {
    String body;
    body.reserve(256);
    body += "{\"feeds\":[";
    File file = Board::Storage::filesystem().open(StoragePaths::kRssConfigPath);
    bool first = true;
    if (file && !file.isDirectory()) {
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            if (line.startsWith("feed=")) {
                line = line.substring(5);
                line.trim();
            }
            if (!isHttpUrl(line)) {
                continue;
            }
            if (!first) {
                body += ",";
            }
            first = false;
            body += "\"" + jsonEscape(line) + "\"";
        }
    }
    if (file) {
        file.close();
    }
    body += "]}";
    return body;
}

bool CompanionSyncManager::writeRssFeedsJson(const String& body, String& error) {
    if (body.length() > kMaxRssFeedsPatchBytes) {
        error = "RSS feed payload too large";
        return false;
    }

    int colonIndex = -1;
    if (!findJsonKey(body, "feeds", colonIndex)) {
        error = "Missing feeds array";
        return false;
    }
    int index = skipJsonWhitespace(body, colonIndex + 1);
    if (index >= static_cast<int>(body.length()) || body[index] != '[') {
        error = "feeds must be an array";
        return false;
    }
    ++index;

    std::vector<String> feeds;
    feeds.reserve(8);
    while (true) {
        index = skipJsonWhitespace(body, index);
        if (index < static_cast<int>(body.length()) && body[index] == ']') {
            break;
        }

        String feed;
        if (!nextJsonArrayString(body, index, feed)) {
            error = "Invalid feeds array";
            return false;
        }
        feed.trim();
        if (feed.isEmpty()) {
            continue;
        }
        if (!isHttpUrl(feed)) {
            error = "Feeds must start with http:// or https://";
            return false;
        }
        if (feeds.size() >= kMaxRssFeeds) {
            error = "Too many RSS feeds";
            return false;
        }
        bool duplicate = false;
        for (const String& existing: feeds) {
            if (existing == feed) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            feeds.push_back(feed);
        }
    }

    Board::Storage::filesystem().mkdir(StoragePaths::kConfigPath);
    const String tmpPath = StoragePaths::kRssConfigTempPath;
    Board::Storage::filesystem().remove(tmpPath);
    File file = Board::Storage::filesystem().open(tmpPath, FILE_WRITE);
    if (!file) {
        error = "Could not write RSS config";
        return false;
    }
    file.println("# RSVP Nano RSS feeds");
    for (const String& feed: feeds) {
        file.print("feed=");
        file.println(feed);
    }
    file.close();

    Board::Storage::filesystem().remove(StoragePaths::kRssConfigPath);
    if (!Board::Storage::filesystem().rename(tmpPath, StoragePaths::kRssConfigPath)) {
        Board::Storage::filesystem().remove(tmpPath);
        error = "Could not save RSS config";
        return false;
    }
    return true;
}

String CompanionSyncManager::deviceSuffix() const {
    uint64_t mac = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned int>(mac & 0xFFFFFF));
    return String(suffix);
}

String CompanionSyncManager::jsonEscape(const String& value) const {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        switch (c) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (c < 0x20) {
                char code[7];
                std::snprintf(code, sizeof(code), "\\u%04x", c);
                escaped += code;
            } else {
                escaped += static_cast<char>(c);
            }
            break;
        }
    }
    return escaped;
}

String CompanionSyncManager::sanitizeFilename(const String& name) const {
    String sanitized;
    sanitized.reserve(name.length());
    for (size_t i = 0; i < name.length(); ++i) {
        const char c = name[i];
        sanitized += isSafeFilenameChar(c) ? c : '-';
    }
    sanitized.trim();
    while (sanitized.startsWith(".")) {
        sanitized.remove(0, 1);
    }
    return sanitized;
}

CompanionSyncManager::RsvpMetadata CompanionSyncManager::readRsvpMetadata(const String& path) const {
    RsvpMetadata metadata;
    String loweredPath = path;
    loweredPath.toLowerCase();
    if (!loweredPath.endsWith(".rsvp")) {
        return metadata;
    }

    File file = Board::Storage::filesystem().open(path);
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return metadata;
    }

    String line;
    line.reserve(kMaxMetadataLineChars);
    bool pastDirectives = false;
    while (file.available()) {
        const char c = static_cast<char>(file.read());
        if (c == '\r') {
            continue;
        }

        if (c != '\n') {
            line += c;
            if (line.length() > kMaxMetadataLineChars) {
                pastDirectives = true;
                line = "";
                break;
            }
            continue;
        }

        if (metadata.title.isEmpty()) {
            metadata.title = rsvpMetadataValueFromLine(line, "@title", pastDirectives);
        }
        if (metadata.author.isEmpty() && !pastDirectives) {
            metadata.author = rsvpMetadataValueFromLine(line, "@author", pastDirectives);
        }
        if (!metadata.title.isEmpty() && !metadata.author.isEmpty()) {
            break;
        }

        if (pastDirectives) {
            break;
        }
        line = "";
    }

    if (!line.isEmpty() && !pastDirectives) {
        if (metadata.title.isEmpty()) {
            metadata.title = rsvpMetadataValueFromLine(line, "@title", pastDirectives);
        }
        if (metadata.author.isEmpty() && !pastDirectives) {
            metadata.author = rsvpMetadataValueFromLine(line, "@author", pastDirectives);
        }
    }

    file.close();
    return metadata;
}

bool CompanionSyncManager::progressForPath(const String& path, uint32_t sourceSize, uint32_t sourceFingerprint,
                                           uint32_t wordCount, uint32_t& wordIndex, uint8_t& percent) {
    if (wordCount <= 1) {
        return false;
    }

    if (ReadingProgress::readPositionSidecar(path, {sourceSize, sourceFingerprint, wordCount}, wordIndex)) {
        percent = ReadingProgress::percent(wordIndex, wordCount);
        return true;
    }

    if (!ReadingProgress::readCachedPosition(*preferences_, path, {sourceSize, sourceFingerprint, wordCount},
                                             wordIndex))
        return false;
    percent = ReadingProgress::percent(wordIndex, wordCount);
    return true;
}

String CompanionSyncManager::bookIdForPath(const String& path) const {
    return ReadingProgress::bookId(path);
}

bool CompanionSyncManager::resolveBookId(const String& id, String& path) const {
    const char* directories[] = {
        StoragePaths::kBooksPath,
        StoragePaths::kBookFilesPath,
        StoragePaths::kArticleFilesPath,
    };

    for (const char* directoryPath: directories) {
        File dir = Board::Storage::filesystem().open(directoryPath);
        if (!dir || !dir.isDirectory()) {
            if (dir) {
                dir.close();
            }
            continue;
        }

        File entry = dir.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) {
                const String name = displayNameForPath(String(entry.name()));
                String lowered = name;
                lowered.toLowerCase();
                if (isSupportedBookName(lowered)) {
                    const String candidate = String(directoryPath) + "/" + name;
                    if (bookIdForPath(candidate) == id) {
                        path = candidate;
                        entry.close();
                        dir.close();
                        return true;
                    }
                }
            }
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();
    }

    return false;
}

void CompanionSyncManager::finishUpload(bool success) {
    if (uploadFile_) {
        uploadFile_.close();
    }

    if (uploadTmpPath_.isEmpty()) {
        return;
    }

    if (success && uploadError_.isEmpty()) {
        if (!replaceUploadedFile(uploadTmpPath_, uploadFinalPath_)) {
            uploadError_ = "Rename failed";
            Board::Storage::filesystem().remove(uploadTmpPath_);
        } else {
            statusLine1_ = "Book received";
            statusLine2_ = uploadFinalPath_.c_str();
            Serial.printf("[sync] upload ready %s\n", uploadFinalPath_.c_str());
        }
    } else {
        Board::Storage::filesystem().remove(uploadTmpPath_);
    }

    uploadTmpPath_ = "";
}
