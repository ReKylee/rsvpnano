#include "sync/CompanionSyncManager.h"
#include <esp_log.h>
#include "logging/Logger.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <uri/UriBraces.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "board/BoardStorage.h"

#include "converter/EpubZip.h"
#include "display/ThemeStore.h"
#include "fonts/FontCatalog.h"
#include "fonts/RFont4Format.h"
#include "net/WifiConnection.h"
#include "rss/RssConfig.h"
#include "rss/RssConfigStorage.h"
#include "settings/SettingsCodec.h"
#include "settings/SettingsGlaze.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/IndexedBook.h"
#include "storage/index/ReadingProgress.h"
#include "sync/CompanionSyncJson.h"
#include "text/AsciiText.h"
#include "text/LocaleTag.h"
#include "text/RsvpDirectives.h"
#include "text/UnicodeText.h"
#include "timer/FocusTimerStorage.h"
#include "ui/Localization.h"
#include "update/OtaUpdater.h"

namespace {

    namespace api = companion::api;

    constexpr size_t kMaxMetadataLineChars = 160;
    constexpr size_t kMaxRssFeedsPatchBytes = 4096;
    constexpr size_t kMaxFocusTimersBytes = 4096;
    constexpr size_t kMaxThemeUploadBytes = 4096;
    constexpr size_t kMaxFontUploadBytes = 96UL * 1024UL * 1024UL;
    constexpr size_t kMaxLocalePackUploadBytes = 256UL * 1024UL;
    constexpr size_t kMaxLocalePackFiles = 4;

    std::vector<std::string> scriptNames(uint32_t mask) {
        std::vector<std::string> result;
        result.reserve(UnicodeText::SupportedScripts.size());
        for (const UnicodeText::ScriptTag& script: UnicodeText::SupportedScripts) {
            if ((mask & script.mask) != 0)
                result.emplace_back(script.tag);
        }
        return result;
    }

    std::vector<std::string> capabilityNames(uint32_t mask) {
        std::vector<std::string> result;
        result.reserve(UnicodeText::SupportedCapabilities.size());
        for (const UnicodeText::CapabilityTag& capability: UnicodeText::SupportedCapabilities) {
            if ((mask & capability.mask) != 0)
                result.emplace_back(capability.tag);
        }
        return result;
    }

    template<typename T>
    bool sendData(WebServer& server, std::string& jsonBuffer, int status, const T& data) {
        if (auto encoded = api::encodeData(data, jsonBuffer); !encoded) {
            ESP_LOGE("sync", "response encode failed: %s", encoded.error().c_str());
            server
                .send(500, "application/json",
                      "{\"error\":{\"code\":\"serialization_failed\",\"message\":\"Response could not be encoded\"}}");
            return false;
        }
        server.sendHeader("Cache-Control", "no-store");
        server.send(status, "application/json", jsonBuffer.c_str());
        return true;
    }

    std::expected<void, std::error_code> replaceUploadedFile(const std::string& tmpPath, const std::string& finalPath) {
        const std::string backupPath = finalPath + ".bak";
        return StorageFiles::replaceFileAtomic(Board::Storage::filesystem(), finalPath.c_str(), tmpPath.c_str(),
                                               backupPath.c_str());
    }

    std::expected<std::string, std::string> installLocaleArchive(std::string_view archivePath,
                                                                  locales::Catalog& catalog) {
        EpubZip::Archive archive;
        if (!archive.open(archivePath))
            return std::unexpected("Locale pack is not a supported ZIP archive");

        const auto entries = archive.entries();
        if (entries.empty() || entries.size() > kMaxLocalePackFiles)
            return std::unexpected("Locale pack has an invalid file count");

        std::string id;
        for (const auto& entry: entries) {
            const auto candidate = locales::packIdFromArchiveManifest(entry.name);
            if (!candidate)
                continue;
            if (!id.empty())
                return std::unexpected("Locale pack must contain one valid manifest path");
            id = *candidate;
        }
        if (id.empty())
            return std::unexpected("Locale pack manifest is missing");

        const std::string root = "locales/" + id + "/";
        if (auto staged = locales::beginStaging(Board::Storage::filesystem(), id); !staged)
            return std::unexpected(staged.error());

        for (size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            if (!entry.name.starts_with(root))
                return std::unexpected("Locale pack contains files outside its package folder");
            const std::string_view relative = std::string_view{entry.name}.substr(root.size());
            if (!locales::isValidPackFilePath(relative))
                return std::unexpected("Locale pack contains an invalid file path");
            for (size_t previous = 0; previous < index; ++previous) {
                if (entries[previous].name == entry.name)
                    return std::unexpected("Locale pack contains duplicate files");
            }

            const size_t maximum = relative == "manifest.toml" ? locales::kMaximumManifestBytes
                                  : relative == "ui/font.u8g2" ? locales::kMaximumUiFontBytes
                                                               : locales::kMaximumResidentUiBytes;
            std::string contents;
            if (!archive.extractToString(entry.name, contents, maximum))
                return std::unexpected("Locale pack contains an invalid compressed file");
            auto target = locales::prepareStagedFile(Board::Storage::filesystem(), id, relative);
            if (!target)
                return std::unexpected(target.error());
            File output = Board::Storage::filesystem().open(target->c_str(), FILE_WRITE);
            if (!output || output.write(reinterpret_cast<const uint8_t*>(contents.data()), contents.size())
                           != contents.size()) {
                if (output)
                    output.close();
                return std::unexpected("Locale pack file could not be staged");
            }
            output.close();
        }

        archive.close();
        if (auto activated = locales::activateStaged(Board::Storage::filesystem(), catalog, id); !activated)
            return std::unexpected(activated.error());
        return id;
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
h1{font-size:1.15rem;margin:0 0 10px}.tabs{display:flex;gap:6px;overflow-x:auto;padding-bottom:2px}
button,.button{border:1px solid var(--line);border-radius:8px;background:#111714;color:var(--fg);padding:9px 11px;font:inherit}
button.primary,.button.primary{background:var(--accent);border-color:var(--accent);color:var(--accentInk);font-weight:700}button.danger{color:var(--accent2)}
.tabs button{flex:0 0 auto;white-space:nowrap;padding:8px 10px}.tabs button.active{background:var(--fg);color:var(--bg)}
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
<button data-tab="languages">Languages</button>
<button data-tab="fonts">Fonts</button>
<button data-tab="rss">RSS</button>
<button data-tab="focus">Focus</button>
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
<label>Reading mode</label><select id="readingMode"><option value="rsvp">RSVP</option><option value="page">Page</option></select>
<label>Pause behaviour</label><select id="pauseMode"><option value="sentenceEnd">End of sentence</option><option value="instant">Instant</option></select>
<label>Base speed <span id="wpmValue"></span></label><input id="wpm" type="range" min="10" max="1000" step="10">
<label>Long words <span id="longWordMsValue"></span></label><input id="longWordMs" type="range" min="0" max="600" step="50">
<label>Complexity <span id="complexWordMsValue"></span></label><input id="complexWordMs" type="range" min="0" max="600" step="50">
<label>Punctuation <span id="punctuationMsValue"></span></label><input id="punctuationMs" type="range" min="0" max="600" step="50">
</div>
<div class="card"><h2>Display</h2>
<label>Theme</label><select id="themeId"></select>
<label>Online theme</label><select id="onlineThemeId"></select>
<div class="row"><button id="installOnlineThemeButton">Install online theme</button></div>
<label>Theme file</label><input id="themeFileInput" type="file" accept=".toml">
<div class="row"><button id="uploadThemeButton">Upload theme file</button></div>
<label>Brightness <span id="brightnessValue"></span></label><input id="brightnessPercent" type="range" min="5" max="100" step="5">
<label>Reader hand</label><select id="handedness"><option value="right">Right</option><option value="left">Left</option></select>
<label>Footer label</label><select id="footerMetric"><option value="percentage">Percentage</option><option value="chapterTime">Chapter time</option><option value="bookTime">Book time</option></select>
<label>Battery label</label><select id="batteryLabel"><option value="percentage">Percentage</option><option value="timeRemaining">Time remaining</option><option value="voltage">Voltage</option></select>
<label><input id="batteryIcon" type="checkbox" style="width:auto"> Show battery icon</label>
<label><input id="readingBattery" type="checkbox" style="width:auto"> Show battery while reading</label>
<label><input id="readingChapter" type="checkbox" style="width:auto"> Show chapter while reading</label>
<label><input id="readingProgress" type="checkbox" style="width:auto"> Show book percent while reading</label>
</div>
<div class="card"><h2>Typography</h2>
<label>Font size <span id="fontSizeValue"></span></label><input id="fontSizeIndex" type="range" min="0" max="3">
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

<section id="languages" class="page">
<div class="grid">
<div class="card"><h2>Interface language</h2>
<p class="muted">Reader language support comes from installed fonts; locale packs change only the interface.</p>
<label>Language</label><select id="interfaceLocale"><option value="en">English</option></select>
<p><button class="primary" id="saveLocaleButton">Save interface language</button></p>
</div>
<div class="card"><h2>Install locale pack</h2>
<label>Online locale pack</label><select id="onlineLocaleId"></select>
<div class="row"><button id="installOnlineLocaleButton">Install online pack</button></div>
<label>Local locale-pack ZIP</label><input id="localePackFile" type="file" accept=".zip,application/zip">
<div class="row"><button class="primary" id="installLocalePackButton">Install ZIP</button><button id="refreshLocalesButton">Refresh</button></div>
</div>
</div>
<div class="card"><h2>Installed locale packs</h2><div id="localesList" class="muted">Loading...</div></div>
</section>

<section id="fonts" class="page">
<div class="grid">
<div class="card"><h2>Global reader font</h2>
<p class="muted">Used unless a book selects another compatible font for one of its languages.</p>
<label>Typeface</label><select id="typeface"><option value="literata">Literata</option></select>
<p><button class="primary" id="saveFontButton">Save reader font</button></p>
</div>
<div class="card"><h2>Install reader font</h2>
<label>Online font</label><select id="onlineFontId"></select>
<div class="row"><button id="installOnlineFontButton">Install online font</button></div>
<label>Font family</label><input id="fontFamilyName" placeholder="Font folder name">
<label>RFont4 file</label><input id="fontFileInput" type="file" accept=".rfont4">
<div class="row"><button id="uploadFontButton">Upload font file</button></div>
</div>
</div>
<div class="card"><h2>Installed reader fonts</h2><div id="fontsList" class="muted">Loading...</div></div>
</section>

<section id="rss" class="page">
<div class="card"><h2>RSS Feeds</h2><p class="muted">Add one feed URL per line. Feeds are saved to <code>/config/rss.toml</code>; run RSS feeds from the reader menu to download articles.</p>
<textarea id="rssFeeds" placeholder="https://example.com/feed/"></textarea>
<p><button class="primary" id="saveRssButton">Save feeds</button> <button id="reloadRssButton">Reload</button></p>
</div>
</section>

<section id="focus" class="page">
<div class="card"><h2>Focus Timers</h2><p class="muted">Configure the same timers shown on the reader.</p>
<div id="focusTimers"></div>
<p><button id="addFocusButton">Add timer</button> <button class="primary" id="saveFocusButton">Save timers</button></p>
</div>
</section>

<section id="help" class="page">
<div class="card"><h2>How to use this web companion</h2>
<ul>
<li>Open Companion Sync on the reader, then use the address it shows. If it starts an <code>RSVP-Nano</code> network, join that network first.</li>
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
const $=id=>document.getElementById(id);let settings=null,rssConfig={feeds:[]},focusTimers={timers:[]};let deviceThemes=[],deviceFonts=[],deviceLocales={locales:[],rejected:[]};let themeCatalog=[];let themeCatalogUrl='';let fontCatalog=[];let fontCatalogUrl='';let localeCatalog=[];let localeCatalogUrl='';
function status(msg){$('status').textContent=msg}
function catalogUrl(path){const u=(settings&&settings.updates)||{};let owner=String(u.repositoryOwner||'').trim(),repo='rsvpnano',tag=String(u.releaseTag||'').trim();const apply=v=>{const p=v.trim().split('/');if(p.length!==2||!p[0]||!p[1])return false;owner=p[0];repo=p[1];return true};apply(owner);const at=tag.indexOf('@');if(at>0&&at<tag.length-1){const r=tag.slice(0,at).trim();tag=tag.slice(at+1).trim();if(!apply(r)&&r)repo=r}if(!owner||!repo)throw new Error('Configure a GitHub release owner first.');return 'https://raw.githubusercontent.com/'+[owner,repo,tag||'main'].map(encodeURIComponent).join('/')+'/'+path}
async function api(path,opts){const r=await fetch(path,opts);const t=await r.text();let j={};try{j=t?JSON.parse(t):{}}catch(e){throw new Error(t||'Bad response')}if(!r.ok)throw new Error((j.error&&j.error.message)||r.statusText);return j.data}
function bytes(n){return n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':(n/1048576).toFixed(1)+' MB'}
function safeName(s){return (s||'article').replace(/[^a-z0-9._ -]+/gi,'-').replace(/\s+/g,' ').trim().slice(0,72)||'article'}
function escRsvp(s){return (s||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n').trim()}
function articleFile(){const title=$('articleTitle').value.trim()||'Untitled Article';const author=$('articleAuthor').value.trim();const body=escRsvp($('articleBody').value);let out='@rsvp 1\n@title '+title+'\n';if(author)out+='@author '+author+'\n';out+='@para\n'+body+'\n';return {name:safeName(title)+'.rsvp',blob:new Blob([out],{type:'text/plain'})}}
function html(s){return String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function renderList(id,items){$(id).innerHTML=items.length?items.map(b=>`<div class="item"><div class="item-title">${html(b.metadata.title||b.name)}</div><div class="item-meta">${html([b.metadata.author,b.metadata.wordCount?b.metadata.wordCount+' words':null,b.metadata.chapterCount?b.metadata.chapterCount+' chapters':null,bytes(b.bytes),b.reading?b.reading.percent+'% read':null].filter(Boolean).join(' - '))}</div><p><button class="danger" data-delete="${html(encodeURIComponent(b.id))}">Delete</button></p></div>`).join(''):'<span class="muted">Nothing here yet.</span>';document.querySelectorAll('[data-delete]').forEach(b=>b.onclick=()=>delBook(decodeURIComponent(b.dataset.delete)))}
async function refresh(){try{const info=await api('/api/v1/device');$('infoBox').innerHTML=`${info.name}<br><span class="muted">${info.mode} - ${info.networkSsid||''}</span>`;const data=await api('/api/v1/library');renderList('booksList',data.books.filter(b=>b.category!=='article'));renderList('articlesList',data.books.filter(b=>b.category==='article'));status('Connected to RSVP Nano.')}catch(e){status('Connection problem: '+e.message)}}
async function delBook(id){if(!confirm('Delete this item?'))return;try{await api('/api/v1/library?id='+encodeURIComponent(id),{method:'DELETE'});await refresh();status('Deleted')}catch(e){status('Delete failed: '+e.message)}}
async function uploadBlob(blob,name,category){const fd=new FormData();fd.append('file',blob,name);await api('/api/v1/library?name='+encodeURIComponent(name)+'&category='+encodeURIComponent(category),{method:'POST',body:fd})}
async function uploadPicked(inputId,category){const f=$(inputId).files[0];if(!f){status('Choose a file first.');return}try{await uploadBlob(f,f.name,category);$(inputId).value='';await refresh();status('Uploaded '+f.name)}catch(e){status('Upload failed: '+e.message)}}
async function uploadThemeBlob(blob,name){const fd=new FormData();fd.append('file',blob,name);return api('/api/v1/appearance/themes?name='+encodeURIComponent(name),{method:'POST',body:fd})}
async function uploadPickedTheme(){const f=$('themeFileInput').files[0];if(!f){status('Choose a theme file first.');return}try{const uploaded=await uploadThemeBlob(f,f.name);settings.interface.selectedThemeId=uploaded.id;settings=await api('/api/v1/settings',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(settings)});$('themeFileInput').value='';await loadSettings();status('Uploaded theme '+f.name)}catch(e){status('Theme upload failed: '+e.message)}}
async function loadThemeCatalog(){try{themeCatalogUrl=catalogUrl('themes/index.json');themeCatalog=await fetch(themeCatalogUrl,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Catalog unavailable');return r.json()});$('onlineThemeId').innerHTML=themeCatalog.map(t=>`<option value="${html(t.id)}">${html(t.name)}</option>`).join('')}catch(e){$('onlineThemeId').innerHTML='<option value="">Catalog unavailable</option>'}}
async function installOnlineTheme(){const id=val('onlineThemeId');const theme=themeCatalog.find(t=>t.id===id);if(!theme){status('Choose an online theme first.');return}try{const url=new URL(theme.file,themeCatalogUrl).toString();const blob=await fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Theme unavailable');return r.blob()});const uploaded=await uploadThemeBlob(blob,theme.file);settings.interface.selectedThemeId=uploaded.id;settings=await api('/api/v1/settings',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(settings)});await loadSettings();status('Installed '+theme.name)}catch(e){status('Online theme install failed: '+e.message)}}
function fontFamilyFromName(name){return safeName(String(name||'font').replace(/\.rfont4$/i,'').replace(/[-_ ]?(large|medium|small)$/i,''))||'font'}
async function uploadFontBlob(blob,family,name){const fd=new FormData();fd.append('file',blob,name||'font.rfont4');await api('/api/v1/appearance/fonts?family='+encodeURIComponent(family),{method:'POST',body:fd})}
async function uploadPickedFont(){const f=$('fontFileInput').files[0];if(!f){status('Choose a font file first.');return}const family=$('fontFamilyName').value.trim()||fontFamilyFromName(f.name);try{await uploadFontBlob(f,family,f.name);$('fontFileInput').value='';$('fontFamilyName').value='';await loadSettings();status('Uploaded '+family)}catch(e){status('Font upload failed: '+e.message)}}
async function loadFontCatalog(){try{fontCatalogUrl=catalogUrl('fonts/index.json');fontCatalog=await fetch(fontCatalogUrl,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Catalog unavailable');return r.json()});$('onlineFontId').innerHTML=fontCatalog.map(f=>`<option value="${html(f.id)}">${html(f.name)}</option>`).join('')}catch(e){$('onlineFontId').innerHTML='<option value="">Catalog unavailable</option>'}}
async function installOnlineFont(){const id=val('onlineFontId'),font=fontCatalog.find(f=>f.id===id);if(!font||!font.file){status('Choose an online font first.');return}try{const url=new URL(font.file,fontCatalogUrl).toString();const blob=await fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Font unavailable');return r.blob()});await uploadFontBlob(blob,font.name||font.id,font.file.split('/').pop()||'font.rfont4');await loadSettings();status('Installed '+(font.name||font.id))}catch(e){status('Online font install failed: '+e.message)}}
async function syncArticle(){const f=articleFile();if(!$('articleBody').value.trim()){status('Paste article text first.');return}try{await uploadBlob(f.blob,f.name,'article');localStorage.removeItem('rsvpArticleDraft');await refresh();status('Synced '+f.name)}catch(e){status('Article sync failed: '+e.message)}}
function saveDraft(){localStorage.setItem('rsvpArticleDraft',JSON.stringify({title:$('articleTitle').value,author:$('articleAuthor').value,body:$('articleBody').value}));status('Draft saved in this browser.')}
function loadDraft(){try{const d=JSON.parse(localStorage.getItem('rsvpArticleDraft')||'{}');$('articleTitle').value=d.title||'';$('articleAuthor').value=d.author||'';$('articleBody').value=d.body||''}catch(e){}}
function val(id){const e=$(id);return e.type==='checkbox'?e.checked:e.value}
function setVal(id,v){const e=$(id);if(e.type==='checkbox')e.checked=!!v;else e.value=v}
function setThemeOptions(){const id=(settings&&settings.interface&&settings.interface.selectedThemeId)||'default';const themes=deviceThemes.some(t=>t.id===id)?deviceThemes:[...deviceThemes,{id,name:id}];$('themeId').innerHTML=themes.map(t=>`<option value="${html(t.id)}">${html(t.name||t.id)}</option>`).join('');setVal('themeId',id)}
function setFontOptions(){const id=(settings&&settings.reading&&settings.reading.typography&&settings.reading.typography.fontId)||'literata';const fonts=deviceFonts.some(f=>f.id===id)?deviceFonts:[...deviceFonts,{id,name:id}];$('typeface').innerHTML=fonts.map(f=>`<option value="${html(f.id)}">${html(f.name||f.id)}</option>`).join('');setVal('typeface',id)}
function renderFonts(){$('fontsList').innerHTML=deviceFonts.map(f=>`<div class="item"><div class="item-title">${html(f.name||f.id)}</div><div class="item-meta">${html([...(f.locales||[]),...(f.scripts||[]),f.builtIn?'Built in':''].filter(Boolean).join(' - '))}</div>${f.builtIn?'':`<p><button class="danger" data-delete-font="${html(encodeURIComponent(f.id))}">Remove</button></p>`}</div>`).join('');document.querySelectorAll('[data-delete-font]').forEach(b=>b.onclick=()=>removeFont(decodeURIComponent(b.dataset.deleteFont)))}
async function removeFont(id){if(!confirm('Remove font '+id+'?'))return;try{await api('/api/v1/appearance/fonts?id='+encodeURIComponent(id),{method:'DELETE'});await loadSettings();status('Removed font '+id)}catch(e){status('Font removal failed: '+e.message)}}
function setLocaleOptions(){const current=(settings&&settings.interface&&settings.interface.locale)||'en';const locales=new Map([['en','English']]);(deviceLocales.locales||[]).filter(p=>p.locale).forEach(p=>locales.set(p.locale,p.nativeName||p.englishName||p.locale));if(!locales.has(current))locales.set(current,current);$('interfaceLocale').innerHTML=[...locales].map(([id,name])=>`<option value="${html(id)}">${html(name)}</option>`).join('');setVal('interfaceLocale',current)}
function renderLocales(){const packs=deviceLocales.locales||[],rejected=deviceLocales.rejected||[];let out=packs.map(p=>`<div class="item"><div class="item-title">${html(p.nativeName||p.englishName||p.id)}</div><div class="item-meta">${html([p.id,p.version,p.locale,p.direction].filter(Boolean).join(' - '))}</div><p><button class="danger" data-delete-locale="${html(encodeURIComponent(p.id))}">Remove</button></p></div>`).join('');if(!out)out='<span class="muted">No external locale packs installed.</span>';if(rejected.length)out+=`<p class="muted">Rejected: ${rejected.map(i=>html(i.id+': '+i.reason)).join('; ')}</p>`;$('localesList').innerHTML=out;document.querySelectorAll('[data-delete-locale]').forEach(b=>b.onclick=()=>removeLocalePack(decodeURIComponent(b.dataset.deleteLocale)));setLocaleOptions()}
async function loadLocales(){try{deviceLocales=await api('/api/v1/locales');renderLocales()}catch(e){status('Locale packs load failed: '+e.message)}}
async function uploadLocalePackBlob(blob,name){const fd=new FormData();fd.append('file',blob,name||'locale.zip');return api('/api/v1/locales',{method:'POST',body:fd})}
async function loadLocaleCatalog(){try{localeCatalogUrl=catalogUrl('locale-packs/index.json');localeCatalog=await fetch(localeCatalogUrl,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Catalog unavailable');return r.json()});$('onlineLocaleId').innerHTML=localeCatalog.map(p=>`<option value="${html(p.id)}">${html(p.name||p.id)}</option>`).join('')}catch(e){$('onlineLocaleId').innerHTML='<option value="">Catalog unavailable</option>'}}
async function installOnlineLocale(){const id=val('onlineLocaleId'),pack=localeCatalog.find(p=>p.id===id);if(!pack||!pack.file){status('Choose an online locale pack first.');return}try{const url=new URL(pack.file,localeCatalogUrl).toString();const blob=await fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Locale pack unavailable');return r.blob()});await uploadLocalePackBlob(blob,pack.file);await loadLocales();status('Installed '+(pack.name||pack.id))}catch(e){status('Online locale install failed: '+e.message)}}
async function installLocalePack(){const file=$('localePackFile').files[0];if(!file){status('Choose a locale-pack ZIP first.');return}try{await uploadLocalePackBlob(file,file.name);$('localePackFile').value='';await loadLocales();status('Installed '+file.name)}catch(e){status('Locale pack install failed: '+e.message)}}
async function removeLocalePack(id){if(!confirm('Remove locale pack '+id+'?'))return;try{await api('/api/v1/locales/'+encodeURIComponent(id),{method:'DELETE'});await loadLocales();status('Removed locale pack '+id)}catch(e){status('Locale pack removal failed: '+e.message)}}
function snapWpm(v){v=Math.max(10,Math.min(1000,Math.round(+v||300)));return Math.round(v/10)*10}
function updateLabels(){['wpm','longWordMs','complexWordMs','punctuationMs','brightnessPercent','fontSizeIndex','tracking','anchorPercent','guideWidth','guideGap'].forEach(id=>{const l=$(id+'Value')||$(id.replace('Percent','')+'Value')||$(id.replace('Index','')+'Value');if(l)l.textContent=$(id).value+(id==='wpm'?' WPM':id.includes('Ms')?' ms':id==='brightnessPercent'?'%':'')})}
async function loadSettings(){try{[settings,{themes:deviceThemes=[]},{fonts:deviceFonts=[]},deviceLocales]=await Promise.all([api('/api/v1/settings'),api('/api/v1/appearance/themes'),api('/api/v1/appearance/fonts'),api('/api/v1/locales')]);setThemeOptions();setFontOptions();renderFonts();renderLocales();if(!themeCatalog.length)loadThemeCatalog();if(!fontCatalog.length)loadFontCatalog();if(!localeCatalog.length)loadLocaleCatalog();const r=settings.reading,i=settings.interface,t=r.typography,p=r.pacing;setVal('readingMode',r.mode||'rsvp');setVal('pauseMode',r.pauseMode);setVal('wpm',snapWpm(r.wpm));setVal('longWordMs',p.longWordDelayMs);setVal('complexWordMs',p.complexWordDelayMs);setVal('punctuationMs',p.punctuationDelayMs);setVal('themeId',i.selectedThemeId||'default');setVal('interfaceLocale',i.locale||'en');setVal('brightnessPercent',i.brightnessPercent);setVal('handedness',r.leftHanded?'left':'right');setVal('footerMetric',r.footerMetric);setVal('batteryLabel',r.batteryLabel);setVal('batteryIcon',r.batteryIconVisible);setVal('readingBattery',r.batteryVisibleWhileReading);setVal('readingChapter',r.chapterVisibleWhileReading);setVal('readingProgress',r.progressVisibleWhileReading);setVal('typeface',t.fontId);setVal('fontSizeIndex',t.fontSizeIndex);setVal('tracking',t.tracking);setVal('anchorPercent',t.anchor);setVal('guideWidth',t.guideWidth);setVal('guideGap',t.guideGap);setVal('focusHighlight',t.focusHighlight);setVal('phantomWords',r.phantomWords);updateLabels()}catch(e){status('Settings load failed: '+e.message)}}
async function saveSettings(){setVal('wpm',snapWpm(val('wpm')));const r=settings.reading,i=settings.interface,t=r.typography,p=r.pacing;r.wpm=+val('wpm');r.mode=val('readingMode');r.pauseMode=val('pauseMode');p.longWordDelayMs=+val('longWordMs');p.complexWordDelayMs=+val('complexWordMs');p.punctuationDelayMs=+val('punctuationMs');i.selectedThemeId=val('themeId');i.locale=val('interfaceLocale');i.brightnessPercent=+val('brightnessPercent');r.leftHanded=val('handedness')==='left';r.footerMetric=val('footerMetric');r.batteryLabel=val('batteryLabel');r.batteryIconVisible=val('batteryIcon');r.batteryVisibleWhileReading=val('readingBattery');r.chapterVisibleWhileReading=val('readingChapter');r.progressVisibleWhileReading=val('readingProgress');r.phantomWords=val('phantomWords');t.fontId=val('typeface');t.fontSizeIndex=+val('fontSizeIndex');t.focusHighlight=val('focusHighlight');t.tracking=+val('tracking');t.anchor=+val('anchorPercent');t.guideWidth=+val('guideWidth');t.guideGap=+val('guideGap');try{settings=await api('/api/v1/settings',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(settings)});status('Settings saved and applied.')}catch(e){status('Settings save failed: '+e.message)}}
async function loadWifi(){try{await api('/api/v1/network');const ssid=(settings&&settings.network&&settings.network.wifiSsid)||'';$('wifiSsid').value=ssid;$('wifiPassword').value='';$('wifiCurrent').textContent=ssid?'Saved network: '+ssid:'No home Wi-Fi saved.'}catch(e){status('Wi-Fi load failed: '+e.message)}}
async function saveWifi(){const ssid=$('wifiSsid').value.trim();if(!ssid){status('Enter a Wi-Fi SSID first.');return}try{await api('/api/v1/network',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password:$('wifiPassword').value})});settings.network.wifiSsid=ssid;$('wifiPassword').value='';$('wifiCurrent').textContent='Saved network: '+ssid;status('Wi-Fi saved for RSS and OTA.')}catch(e){status('Wi-Fi save failed: '+e.message)}}
async function forgetWifi(){if(!confirm('Forget saved Wi-Fi?'))return;try{await api('/api/v1/network',{method:'DELETE'});settings.network.wifiSsid='';$('wifiSsid').value='';$('wifiPassword').value='';$('wifiCurrent').textContent='No home Wi-Fi saved.';status('Wi-Fi credentials cleared.')}catch(e){status('Forget Wi-Fi failed: '+e.message)}}
async function loadRss(){try{rssConfig=await api('/api/v1/feeds');$('rssFeeds').value=(rssConfig.feeds||[]).join('\n');status('RSS feeds loaded.')}catch(e){status('RSS load failed: '+e.message)}}
async function saveRss(){rssConfig.feeds=$('rssFeeds').value.split(/\n+/).map(s=>s.trim()).filter(Boolean);try{rssConfig=await api('/api/v1/feeds',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(rssConfig)});status('RSS feeds saved.')}catch(e){status('RSS save failed: '+e.message)}}
function renderFocus(){const timers=focusTimers.timers||[];$('focusTimers').innerHTML=timers.map((t,i)=>`<div class="item" data-focus-index="${i}"><label>Name</label><input data-field="name" maxlength="14" value="${html(t.name)}"><div class="row"><label>Focus minutes<input data-field="focusMinutes" type="number" min="1" max="180" value="${t.focusMinutes}"></label><label>Break minutes<input data-field="breakMinutes" type="number" min="1" max="60" value="${t.breakMinutes}"></label><label>Rounds<input data-field="rounds" type="number" min="1" max="12" value="${t.rounds}"></label></div>${timers.length>1?`<button class="danger" data-remove-focus="${i}">Remove</button>`:''}</div>`).join('');document.querySelectorAll('[data-remove-focus]').forEach(b=>b.onclick=()=>{readFocus();focusTimers.timers.splice(+b.dataset.removeFocus,1);renderFocus()})}
function readFocus(){focusTimers.timers=[...document.querySelectorAll('[data-focus-index]')].map(card=>({name:card.querySelector('[data-field=name]').value.trim(),focusMinutes:+card.querySelector('[data-field=focusMinutes]').value,breakMinutes:+card.querySelector('[data-field=breakMinutes]').value,rounds:+card.querySelector('[data-field=rounds]').value}))}
async function loadFocus(){try{focusTimers=await api('/api/v1/focus');renderFocus();status('Focus timers loaded.')}catch(e){status('Focus timer load failed: '+e.message)}}
function addFocus(){readFocus();if(focusTimers.timers.length<6){focusTimers.timers.push({name:'Timer',focusMinutes:25,breakMinutes:5,rounds:4});renderFocus()}}
async function saveFocus(){readFocus();try{focusTimers=await api('/api/v1/focus',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(focusTimers)});renderFocus();status('Focus timers saved.')}catch(e){status('Focus timer save failed: '+e.message)}}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tabs button,.page').forEach(x=>x.classList.remove('active'));b.classList.add('active');$(b.dataset.tab).classList.add('active');if(['settings','languages','fonts'].includes(b.dataset.tab))loadSettings();if(b.dataset.tab==='settings')loadWifi();if(b.dataset.tab==='rss')loadRss();if(b.dataset.tab==='focus')loadFocus()});
$('wpm').oninput=()=>{setVal('wpm',snapWpm(val('wpm')));updateLabels()};
['longWordMs','complexWordMs','punctuationMs','brightnessPercent','fontSizeIndex','tracking','anchorPercent','guideWidth','guideGap'].forEach(id=>$(id).oninput=updateLabels);
$('refreshBooksButton').onclick=refresh;$('refreshArticlesButton').onclick=refresh;$('uploadBookButton').onclick=()=>uploadPicked('bookFileInput','book');$('uploadArticleButton').onclick=()=>uploadPicked('articleFileInput','article');$('uploadThemeButton').onclick=uploadPickedTheme;$('installOnlineThemeButton').onclick=installOnlineTheme;$('uploadFontButton').onclick=uploadPickedFont;$('installOnlineFontButton').onclick=installOnlineFont;$('installOnlineLocaleButton').onclick=installOnlineLocale;$('installLocalePackButton').onclick=installLocalePack;$('refreshLocalesButton').onclick=loadLocales;$('syncArticleButton').onclick=syncArticle;$('saveDraftButton').onclick=saveDraft;$('saveSettingsButton').onclick=saveSettings;$('saveLocaleButton').onclick=saveSettings;$('saveFontButton').onclick=saveSettings;$('saveWifiButton').onclick=saveWifi;$('forgetWifiButton').onclick=forgetWifi;$('saveRssButton').onclick=saveRss;$('reloadRssButton').onclick=loadRss;$('addFocusButton').onclick=addFocus;$('saveFocusButton').onclick=saveFocus;
loadDraft();refresh();
</script>
</body>
</html>)HTML";

    bool isSafeFilenameChar(char c) {
        return AsciiText::isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == ' ';
    }

    std::string ipToString(IPAddress ip) {
        return std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "."
             + std::to_string(ip[3]);
    }

    bool isSupportedBookName(std::string_view loweredName) {
        return loweredName.ends_with(".rsvp") || loweredName.ends_with(".txt") || loweredName.ends_with(".epub");
    }

    std::string displayNameForPath(std::string_view path) {
        const size_t separator = path.rfind('/');
        return std::string{separator == std::string_view::npos ? path : path.substr(separator + 1)};
    }

    std::string relativeLibraryName(std::string_view path) {
        const std::string prefix = std::string{StoragePaths::kBooksPath} + "/";
        if (path.starts_with(prefix)) {
            return std::string{path.substr(prefix.length())};
        }
        return displayNameForPath(path);
    }

    std::string libraryCategoryForPath(std::string_view path) {
        const std::string relative = relativeLibraryName(path);
        if (relative.starts_with("articles/")) {
            return "article";
        }
        if (relative.starts_with("books/")) {
            return "book";
        }
        return "root";
    }

    struct ValidationError {
        std::string message;
        std::string field;
    };

    std::string trimCopy(std::string value) {
        const auto whitespace = [](unsigned char character) {
            return character == ' ' || character == '\t' || character == '\r' || character == '\n';
        };
        while (!value.empty() && whitespace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && whitespace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    }

    api::NetworkResponse makeNetworkResponse(const settings::DeviceSecrets& secrets) {
        return {!secrets.wifiPassword.empty()};
    }

    rss::Config readFeeds() {
        return rss::load(Board::Storage::filesystem()).value_or(rss::Config{});
    }

    std::optional<ValidationError> writeFeeds(rss::Config config) {
        if (auto result = rss::save(Board::Storage::filesystem(), std::move(config)); !result) {
            if (result.error() == std::errc::invalid_argument)
                return ValidationError{"Feeds must start with http:// or https://", "feeds"};
            if (result.error() == std::errc::no_buffer_space)
                return ValidationError{"Too many RSS feeds", "feeds"};
            return ValidationError{"Could not save RSS config", "feeds"};
        }
        return std::nullopt;
    }

    focus::Timers readFocusTimers() {
        return focus::load(Board::Storage::filesystem()).value_or(focus::defaultTimers());
    }

    std::string rsvpMetadataValueFromLine(std::string_view line, const char* directive, bool& pastDirectives) {
        const std::string_view trimmed = RsvpText::stripBom(line);
        if (trimmed.empty()) {
            return "";
        }

        if (RsvpText::prefixHasBoundary(trimmed, directive)) {
            return std::string{RsvpText::directiveValue(trimmed, directive)};
        }

        if (!trimmed.starts_with('@')) {
            pastDirectives = true;
        }
        return "";
    }

} // namespace

bool CompanionSyncManager::begin() {
    if (active_) {
        return true;
    }

    statusLine1_ = "Starting sync";
    statusLine2_ = "Preparing Wi-Fi";
    changes_ = 0;
    jsonBuffer_.clear();

    const bool networkReady = startStation() || startAccessPoint();
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
    ESP_LOGI("sync", "ready ssid=%s url=%s", networkSsid_.c_str(), statusLine2_.c_str());
    return true;
}

uint8_t CompanionSyncManager::update() {
    if (!active_ || !serverStarted_) {
        return false;
    }
    server_.handleClient();
    const uint8_t changes = changes_;
    changes_ = 0;
    return changes;
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
    networkSsid_.clear();
    active_ = false;
    changes_ = 0;
    statusLine1_ = "Idle";
    statusLine2_ = "";
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

bool CompanionSyncManager::startAccessPoint() {
    const std::string ssid = "RSVP-Nano-" + deviceSuffix();
    statusLine1_ = "Sync Wi-Fi";
    statusLine2_ = ssid;
    networkSsid_ = ssid;
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid.c_str())) {
        ESP_LOGE("sync", "softAP failed");
        return false;
    }

    networkMode_ = NetworkMode::AccessPoint;
    ESP_LOGD("sync", "softAP ssid=%s ip=%s", ssid.c_str(), ipToString(WiFi.softAPIP()).c_str());
    return true;
}

bool CompanionSyncManager::startStation() {
    const std::string& ssid = settingsStore_.settings().network.wifiSsid;
    if (ssid.empty()) {
        return false;
    }

    statusLine1_ = "Connecting to Wi-Fi";
    statusLine2_ = ssid;
    auto connected = net::connectStation(ssid.c_str(), settingsStore_.secrets().wifiPassword.c_str());
    if (!connected) {
        ESP_LOGE("sync", "station failed ssid=%s error=%s code=%d; starting access point", ssid.c_str(),
                 connected.error().message().c_str(), connected.error().value());
        net::disconnect();
        return false;
    }

    networkMode_ = NetworkMode::Station;
    networkSsid_ = ssid;
    const std::string suffix = deviceSuffix();
    std::string hostname = "rsvp-nano-" + suffix;
    const std::string instanceName = "RSVP-Nano-" + suffix;
    std::ranges::transform(hostname, hostname.begin(), AsciiText::toLower);
    if (!MDNS.begin(hostname.c_str())) {
        ESP_LOGE("sync", "mDNS failed; starting access point");
        net::disconnect();
        networkMode_ = NetworkMode::None;
        networkSsid_.clear();
        return false;
    }

    MDNS.setInstanceName(instanceName.c_str());
    if (!MDNS.addService("rsvpnano", "tcp", 80)) {
        ESP_LOGE("sync", "mDNS service failed; starting access point");
        MDNS.end();
        net::disconnect();
        networkMode_ = NetworkMode::None;
        networkSsid_.clear();
        return false;
    }
    MDNS.addServiceTxt("rsvpnano", "tcp", "id", suffix.c_str());
    MDNS.addServiceTxt("rsvpnano", "tcp", "api", "1");
    mdnsStarted_ = true;
    ESP_LOGD("sync", "station ssid=%s ip=%s", ssid.c_str(), ipToString(WiFi.localIP()).c_str());
    return true;
}

bool CompanionSyncManager::startServer() {
    server_.on("/", HTTP_GET, [this] { handleRoot(); });
    server_.on("/api/v1/device", HTTP_GET, [this] { handleInfo(); });
    server_.on("/api/v1/library", HTTP_GET, [this] { handleBooksList(); });
    server_.on("/api/v1/library", HTTP_DELETE, [this] { handleBookDelete(); });
    server_.on("/api/v1/library", HTTP_POST, [this] { handleBooks(); }, [this] { handleBookUpload(); });
    server_.on("/api/v1/library/position", HTTP_PATCH, [this] { handleBookPosition(); });
    server_.on("/api/v1/library/language-fonts", HTTP_PATCH, [this] { handleBookLanguageFonts(); });
    server_.on("/api/v1/appearance/themes", HTTP_GET, [this] { handleThemes(); });
    server_.on("/api/v1/appearance/themes", HTTP_POST, [this] { handleThemes(); }, [this] { handleThemeUpload(); });
    server_.on("/api/v1/appearance/fonts", HTTP_GET, [this] { handleFonts(); });
    server_.on("/api/v1/appearance/fonts", HTTP_POST, [this] { handleFonts(); }, [this] { handleFontUpload(); });
    server_.on("/api/v1/appearance/fonts", HTTP_DELETE, [this] { handleFonts(); });
    server_.on("/api/v1/locales", HTTP_GET, [this] { handleLocales(); });
    server_.on("/api/v1/locales", HTTP_POST, [this] { handleLocaleInstall(); },
               [this] { handleLocaleUpload(); });
    server_.on(UriBraces("/api/v1/locales/{}"), HTTP_DELETE, [this] { handleLocaleDelete(); });
    server_.on("/api/v1/settings", HTTP_GET, [this] { handleSettings(); });
    server_.on("/api/v1/settings", HTTP_PUT, [this] { handleSettings(); });
    server_.on("/api/v1/network", HTTP_GET, [this] { handleWifi(); });
    server_.on("/api/v1/network", HTTP_PUT, [this] { handleWifi(); });
    server_.on("/api/v1/network", HTTP_DELETE, [this] { handleWifi(); });
    server_.on("/api/v1/feeds", HTTP_GET, [this] { handleRssFeeds(); });
    server_.on("/api/v1/feeds", HTTP_PUT, [this] { handleRssFeeds(); });
    server_.on("/api/v1/focus", HTTP_GET, [this] { handleFocusTimers(); });
    server_.on("/api/v1/focus", HTTP_PUT, [this] { handleFocusTimers(); });
    server_.onNotFound([this] { handleNotFound(); });
    server_.begin();
    serverStarted_ = true;

    return true;
}

void CompanionSyncManager::stopServer() {
    if (serverStarted_) {
        server_.stop();
    }
    if (mdnsStarted_) {
        MDNS.end();
        mdnsStarted_ = false;
    }
    finishUpload(false);
    serverStarted_ = false;
}

void CompanionSyncManager::handleInfo() {
    sendData(server_, jsonBuffer_, 200,
             api::DeviceInfo{
                 "RSVP Nano",
                 networkMode_ == NetworkMode::Station ? api::NetworkMode::station : api::NetworkMode::access_point,
                 networkSsid_,
                 std::string(OtaUpdater::currentVersion()),
                 Board::Config::OTA_ASSET_NAME,
                 1,
             });
}

void CompanionSyncManager::handleRoot() {
    server_.sendHeader("Cache-Control", "no-store, max-age=0");
    server_.send_P(200, "text/html", kWebCompanionHtml);
}

void CompanionSyncManager::handleBooksList() {
    api::LibraryResponse response;
    const uint16_t wpm = settingsStore_.settings().reading.wpm;

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
                const std::string name = displayNameForPath(entry.name());
                const std::string path = std::string{directoryPath} + "/" + name;
                std::string lowered = name;
                std::ranges::transform(lowered, lowered.begin(), AsciiText::toLower);
                if (isSupportedBookName(lowered)) {
                    api::LibraryItem item;
                    item.id = bookIdForPath(path);
                    item.name = relativeLibraryName(path);
                    item.category = libraryCategoryForPath(path);
                    item.bytes = static_cast<uint32_t>(entry.size());

                    const RsvpMetadata sourceMetadata = readRsvpMetadata(path);
                    BookMetadata indexedMetadata;
                    IndexedBookStore::Header indexHeader;
                    const bool hasIndexedMetadata =
                        IndexedBook::readMetadata({path.c_str(), path.length()}, indexedMetadata, &indexHeader);
                    uint8_t progressPercent = 0;
                    uint32_t wordIndex = 0;
                    settings::ReadingOverrides overrides;
                    if (hasIndexedMetadata)
                        progressForPath(path, indexHeader.sourceSize, indexHeader.sourceFingerprint,
                                        indexHeader.wordCount, wordIndex, progressPercent, overrides);
                    item.metadata.title = hasIndexedMetadata && !indexedMetadata.title.empty() ? indexedMetadata.title
                                                                                               : sourceMetadata.title;
                    item.metadata.author = hasIndexedMetadata && !indexedMetadata.author.empty()
                                             ? indexedMetadata.author
                                             : sourceMetadata.author;
                    item.metadata.wordCount = hasIndexedMetadata ? indexHeader.wordCount : 0;
                    item.metadata.chapterCount =
                        hasIndexedMetadata ? static_cast<uint32_t>(indexedMetadata.chapters.size()) : 0;
                    if (hasIndexedMetadata) {
                        item.metadata.locale = indexedMetadata.locale;
                        item.metadata.direction = toString(indexedMetadata.baseDirection);
                        item.metadata.scriptMask = indexedMetadata.scriptMask;
                        item.metadata.scripts = scriptNames(indexedMetadata.scriptMask);
                        const auto addLanguage = [&](std::string_view locale, uint32_t scripts) {
                            if (locale.empty())
                                return;
                            const auto existing = std::ranges::find(item.metadata.languages, locale,
                                                                    &api::BookLanguage::locale);
                            if (existing == item.metadata.languages.end())
                                item.metadata.languages.push_back({std::string{locale}, scripts});
                            else
                                existing->scriptMask |= scripts;
                        };
                        addLanguage(indexedMetadata.locale,
                                    indexedMetadata.textRuns.empty() ? indexedMetadata.scriptMask : 0);
                        for (const BookTextRun& run: indexedMetadata.textRuns)
                            addLanguage(run.locale, run.scriptMask);
                        item.metadata.requiredCapabilities = capabilityNames(indexedMetadata.requiredCapabilities);
                        item.metadata.chapters.reserve(indexedMetadata.chapters.size());
                        std::ranges::transform(indexedMetadata.chapters, std::back_inserter(item.metadata.chapters),
                                               [](const ChapterMarker& chapter) {
                                                   return api::Chapter{chapter.title,
                                                                       static_cast<uint32_t>(chapter.wordIndex)};
                                               });
                        item.source = api::BookSource{indexHeader.sourceSize, indexHeader.sourceFingerprint};

                        api::BookReading reading;
                        reading.wordIndex = wordIndex;
                        reading.percent = progressPercent;
                        reading.remainingWords =
                            indexHeader.wordCount > wordIndex + 1 ? indexHeader.wordCount - wordIndex - 1 : 0;
                        reading.estimatedMinutes = wpm == 0 ? 0 : (reading.remainingWords + wpm - 1) / wpm;
                        reading.languageFonts = std::move(overrides.languageFonts);
                        if (const ChapterMarker* chapter = indexedMetadata.chapterAt(wordIndex)) {
                            reading.currentChapter =
                                api::CurrentChapter{static_cast<uint32_t>(chapter - indexedMetadata.chapters.data()
                                                                          + 1),
                                                    chapter->title};
                        }
                        item.reading = std::move(reading);
                    }
                    response.books.push_back(std::move(item));
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

    sendData(server_, jsonBuffer_, 200, response);
}

void CompanionSyncManager::handleSettings() {
    if (server_.method() == HTTP_GET) {
        sendData(server_, jsonBuffer_, 200, settingsStore_.settings());
        return;
    }

    ThemeStore themeStore;
    themeStore.loadFromSd();

    const String body = server_.arg("plain");
    if (body.length() > settings::kMaxSettingsBytes) {
        sendError(413, "payload_too_large", "Settings payload exceeds 8 KB");
        return;
    }

    auto decoded = settings::codec::decodeJson({body.c_str(), body.length()}, settings::SettingsSource::Companion);
    if (!decoded) {
        sendError(400, "invalid_json", decoded.error().message.c_str());
        return;
    }

    if (!themeStore.selectById(decoded->interface.selectedThemeId)) {
        sendError(422, "invalid_setting", "selectedThemeId does not match an available theme",
                  "interface.selectedThemeId");
        return;
    }
    if (!fontCatalog_.find(decoded->reading.typography.fontId.c_str())) {
        sendError(422, "invalid_setting", "fontId does not match an available font", "reading.typography.fontId");
        return;
    }
    if (auto replaced = settingsStore_.replace(std::move(*decoded), settings::SettingsSource::Companion); !replaced) {
        sendError(422, "invalid_setting", replaced.error().message.c_str(), replaced.error().path.c_str());
        return;
    }

    changes_ |= Settings;
    sendData(server_, jsonBuffer_, 200, settingsStore_.settings());
}

void CompanionSyncManager::handleWifi() {
    if (server_.method() == HTTP_GET) {
        sendData(server_, jsonBuffer_, 200, makeNetworkResponse(settingsStore_.secrets()));
        return;
    }

    if (server_.method() == HTTP_DELETE) {
        settingsStore_.settings().network.wifiSsid.clear();
        settingsStore_.secrets().wifiPassword.clear();
        settingsStore_.acceptChanges();
        settingsStore_.acceptSecretChanges();
        changes_ |= Network;
        statusLine1_ = "Wi-Fi cleared";
        statusLine2_ = "";
        sendData(server_, jsonBuffer_, 200, makeNetworkResponse(settingsStore_.secrets()));
        return;
    }

    const String body = server_.arg("plain");
    if (body.length() > 512) {
        sendError(413, "payload_too_large", "Wi-Fi payload exceeds 512 bytes");
        return;
    }

    auto update = api::decode<api::NetworkUpdate>({body.c_str(), body.length()});
    if (!update) {
        sendError(400, "invalid_json", update.error().c_str());
        return;
    }
    if (!update->ssid) {
        sendError(422, "invalid_network", "Missing Wi-Fi SSID", "ssid");
        return;
    }

    std::string ssid = trimCopy(*update->ssid);
    if (ssid.empty() || ssid.size() > 32) {
        sendError(422, "invalid_network", ssid.empty() ? "Wi-Fi SSID is required" : "Wi-Fi SSID is too long", "ssid");
        return;
    }
    const std::string password = update->password.value_or("");
    if (password.size() > 64) {
        sendError(422, "invalid_network", "Wi-Fi password is too long", "password");
        return;
    }

    settingsStore_.settings().network.wifiSsid = std::move(ssid);
    settingsStore_.secrets().wifiPassword = password;
    settingsStore_.acceptChanges();
    settingsStore_.acceptSecretChanges();
    changes_ |= Network;
    statusLine1_ = "Wi-Fi saved";
    statusLine2_ = settingsStore_.settings().network.wifiSsid;
    sendData(server_, jsonBuffer_, 200, makeNetworkResponse(settingsStore_.secrets()));
}

void CompanionSyncManager::handleRssFeeds() {
    if (server_.method() == HTTP_GET) {
        sendData(server_, jsonBuffer_, 200, readFeeds());
        return;
    }

    const String body = server_.arg("plain");
    if (body.length() > kMaxRssFeedsPatchBytes) {
        sendError(413, "payload_too_large", "RSS feed payload exceeds 4 KB");
        return;
    }

    auto update = api::decode<rss::Config>({body.c_str(), body.length()});
    if (!update) {
        sendError(400, "invalid_json", update.error().c_str());
        return;
    }
    if (const auto error = writeFeeds(std::move(*update))) {
        sendError(422, "invalid_feed", error->message.c_str(), error->field.c_str());
        return;
    }

    statusLine1_ = "RSS feeds saved";
    statusLine2_ = StoragePaths::kRssConfigPath;
    sendData(server_, jsonBuffer_, 200, readFeeds());
}

void CompanionSyncManager::handleFocusTimers() {
    if (server_.method() == HTTP_GET) {
        sendData(server_, jsonBuffer_, 200, readFocusTimers());
        return;
    }

    const String body = server_.arg("plain");
    if (body.length() > kMaxFocusTimersBytes) {
        sendError(413, "payload_too_large", "Focus timer payload exceeds 4 KB");
        return;
    }

    auto timers = api::decode<focus::Timers>({body.c_str(), body.length()});
    if (!timers) {
        sendError(400, "invalid_json", timers.error().c_str());
        return;
    }
    if (!focus::valid(*timers)) {
        sendError(422, "invalid_focus_timers", "Focus timers are invalid", "timers");
        return;
    }
    auto saved = focus::save(Board::Storage::filesystem(), *timers);
    if (!saved) {
        Logger::failure("sync", "save focus timers", StoragePaths::kFocusConfigPath, saved.error());
        sendError(500, "focus_save_failed", "Could not save focus timers");
        return;
    }

    statusLine1_ = "Focus timers saved";
    statusLine2_ = StoragePaths::kFocusConfigPath;
    sendData(server_, jsonBuffer_, 200, *timers);
}

void CompanionSyncManager::handleBooks() {
    finishUpload(uploadError_.empty());
    if (!uploadError_.empty()) {
        sendError(422, "invalid_upload", uploadError_);
        uploadError_ = "";
        return;
    }

    sendData(server_, jsonBuffer_, 201, api::UploadResponse{uploadFinalPath_});
    uploadFinalPath_ = "";
}

void CompanionSyncManager::handleThemes() {
    if (server_.method() == HTTP_GET) {
        ThemeStore store;
        store.loadFromSd();

        api::ThemesResponse response;
        response.themes.reserve(store.themes().size());
        std::ranges::transform(store.themes(), std::back_inserter(response.themes), [](const auto& theme) {
            return api::ThemeSummary{theme.id, theme.definition.name};
        });
        sendData(server_, jsonBuffer_, 200, response);
        return;
    }

    if (uploadFile_) {
        uploadFile_.close();
    }

    if (!uploadError_.empty()) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        const int status = uploadError_.contains("already exists") ? 409 : 400;
        sendError(status, status == 409 ? "already_exists" : "invalid_upload", uploadError_);
        uploadError_ = "";
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        return;
    }

    if (uploadTmpPath_.empty() || uploadFinalPath_.empty()) {
        sendError(400, "missing_upload", "Theme file is required", "file");
        return;
    }

    File tmpFile = Board::Storage::filesystem().open(uploadTmpPath_.c_str(), FILE_READ);
    const size_t uploadSize = tmpFile ? static_cast<size_t>(tmpFile.size()) : 0;
    if (tmpFile) {
        tmpFile.close();
    }
    if (uploadSize == 0 || uploadSize > kMaxThemeUploadBytes) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_size", "Theme file must be between 1 byte and 4 KB", "file");
        return;
    }

    const std::string id = ui::themes::themeIdFromPath({uploadFinalPath_.c_str(), uploadFinalPath_.length()});
    auto themeText =
        StorageFiles::readTextFile(Board::Storage::filesystem(), uploadTmpPath_.c_str(), kMaxThemeUploadBytes);
    if (!themeText) {
        Logger::failure("sync", "read uploaded theme", uploadTmpPath_.c_str(), themeText.error());
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(500, "storage_error", "Theme upload could not be read");
        return;
    }
    auto decoded = ui::themes::decodeToml(*themeText, id);
    if (!decoded) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_theme", decoded.error().message.c_str(), "file");
        return;
    }

    if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(409, "already_exists", "Theme already exists", "name");
        return;
    }

    auto replaced = replaceUploadedFile(uploadTmpPath_, uploadFinalPath_);
    if (!replaced) {
        Logger::failure("sync", "install theme", uploadTmpPath_.c_str(), uploadFinalPath_.c_str(), replaced.error());
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(500, "storage_error", "Theme could not be saved");
        return;
    }

    statusLine1_ = "Theme received";
    statusLine2_ = uploadFinalPath_.c_str();
    ESP_LOGI("sync", "theme ready %s", uploadFinalPath_.c_str());
    sendData(server_, jsonBuffer_, 201, api::ThemeUploadResponse{uploadFinalPath_, id});
    uploadTmpPath_ = "";
    uploadFinalPath_ = "";
}

void CompanionSyncManager::handleFonts() {
    if (server_.method() == HTTP_GET) {
        api::FontsResponse response;
        response.fonts.reserve(fontCatalog_.families().size());
        std::ranges::transform(fontCatalog_.families(), std::back_inserter(response.fonts), [](const auto& family) {
            api::FontSummary summary{.id = family.id,
                                     .name = family.label,
                                     .scriptMask = family.scriptMask,
                                     .builtIn = family.builtIn,
                                     .shaping = family.shaping};
            for (size_t offset = 0; offset < family.locales.size();) {
                const std::string_view locale{family.locales.data() + offset};
                summary.locales.emplace_back(locale);
                offset += locale.size() + 1;
            }
            for (const auto& script: UnicodeText::SupportedScripts) {
                if ((family.scriptMask & script.mask) != 0)
                    summary.scripts.emplace_back(script.tag);
            }
            return summary;
        });
        sendData(server_, jsonBuffer_, 200, response);
        return;
    }

    if (server_.method() == HTTP_DELETE) {
        const String id = server_.arg("id");
        if (id.isEmpty()) {
            sendError(400, "missing_field", "Font id is required", "id");
            return;
        }
        const auto family = fontCatalog_.find({id.c_str(), id.length()});
        if (!family) {
            sendError(404, "font_not_found", "Font not found", "id");
            return;
        }
        if (family->get().builtIn) {
            sendError(422, "builtin_font", "The built-in font cannot be removed", "id");
            return;
        }

        const std::string fontId = family->get().id;
        const std::string path = family->get().path;
        if (settingsStore_.settings().reading.typography.fontId == fontId) {
            settingsStore_.settings().reading.typography.fontId = settings::TypographySettings{}.fontId;
            if (auto accepted = settingsStore_.acceptChanges(); !accepted) {
                sendError(500, "storage_error", "Default font selection could not be saved");
                return;
            }
            changes_ |= Settings;
        }

        fontCatalog_.clearLoaded();
        if (!Board::Storage::filesystem().remove(path.c_str())) {
            fontCatalog_.loadFromSd();
            sendError(500, "storage_error", "Font could not be removed");
            return;
        }
        const size_t separator = path.rfind('/');
        if (separator != std::string::npos)
            Board::Storage::filesystem().rmdir(path.substr(0, separator).c_str());
        fontCatalog_.loadFromSd();
        changes_ |= Fonts;
        statusLine1_ = "Font removed";
        statusLine2_ = fontId.c_str();
        ESP_LOGI("sync", "removed font %s", path.c_str());
        sendData(server_, jsonBuffer_, 200, api::DeleteResponse{fontId, true});
        return;
    }

    if (uploadFile_) {
        uploadFile_.close();
    }

    if (!uploadError_.empty()) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        const int status = uploadError_.contains("already exists") ? 409 : 400;
        sendError(status, status == 409 ? "already_exists" : "invalid_upload", uploadError_);
        uploadError_ = "";
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        return;
    }

    if (uploadTmpPath_.empty() || uploadFinalPath_.empty()) {
        sendError(400, "missing_upload", "Font file is required", "file");
        return;
    }

    File tmpFile = Board::Storage::filesystem().open(uploadTmpPath_.c_str(), FILE_READ);
    const size_t uploadSize = tmpFile ? static_cast<size_t>(tmpFile.size()) : 0;
    if (tmpFile) {
        tmpFile.close();
    }
    if (uploadSize == 0 || uploadSize > kMaxFontUploadBytes) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_size", "Font file must be between 1 byte and 96 MB", "file");
        return;
    }

    auto validated = FontCatalog::inspectFontFile(uploadTmpPath_);
    if (!validated) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(422, "invalid_font", validated.error().c_str(), "file");
        return;
    }
    if (fontCatalog_.find(validated->id)) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(409, "already_exists", "Font family already exists", "family");
        return;
    }

    if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(409, "already_exists", "Font family already exists", "family");
        return;
    }

    auto replaced = replaceUploadedFile(uploadTmpPath_, uploadFinalPath_);
    if (!replaced) {
        Logger::failure("sync", "install font", uploadTmpPath_.c_str(), uploadFinalPath_.c_str(), replaced.error());
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadTmpPath_ = "";
        uploadFinalPath_ = "";
        sendError(500, "storage_error", "Font could not be saved");
        return;
    }

    statusLine1_ = "Font received";
    statusLine2_ = uploadFinalPath_.c_str();
    ESP_LOGI("sync", "font ready %s", uploadFinalPath_.c_str());
    validated->path = uploadFinalPath_.c_str();
    fontCatalog_.addFamily(std::move(*validated));
    sendData(server_, jsonBuffer_, 201, api::UploadResponse{uploadFinalPath_});
    changes_ |= Fonts;
    uploadTmpPath_ = "";
    uploadFinalPath_ = "";
}

void CompanionSyncManager::handleLocales() {
    api::LocalesResponse response;
    response.locales.reserve(localeCatalog_.packs.size());
    std::ranges::transform(localeCatalog_.packs, std::back_inserter(response.locales), [](const auto& pack) {
        const auto& manifest = pack.manifest;
        return api::LocaleSummary{
            .id = manifest.id,
            .version = manifest.version,
            .locale = manifest.locale,
            .nativeName = manifest.nativeName,
            .englishName = manifest.englishName,
            .direction = std::string{toString(manifest.direction)},
            .translationStatus = std::string{locales::toString(manifest.translationStatus)},
            .scriptMask = pack.scriptMask,
            .requiredCapabilities = manifest.requiredCapabilities,
            .scripts = manifest.scripts,
        };
    });
    response.rejected.reserve(localeCatalog_.rejected.size());
    std::ranges::transform(localeCatalog_.rejected, std::back_inserter(response.rejected), [](const auto& issue) {
        return api::LocaleIssue{.id = issue.id, .reason = issue.reason};
    });
    sendData(server_, jsonBuffer_, 200, response);
}

void CompanionSyncManager::handleLocaleInstall() {
    if (uploadFile_)
        uploadFile_.close();
    const auto resetUpload = [this] {
        uploadError_.clear();
        uploadTmpPath_.clear();
        uploadFinalPath_.clear();
    };
    if (!uploadError_.empty()) {
        if (!uploadTmpPath_.empty())
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        const std::string error = uploadError_;
        resetUpload();
        sendError(422, "invalid_locale_pack", error, "file");
        return;
    }
    if (uploadTmpPath_.empty()) {
        resetUpload();
        sendError(400, "missing_upload", "Locale-pack ZIP is required", "file");
        return;
    }

    const std::string archivePath = uploadTmpPath_;
    auto installed = installLocaleArchive(archivePath, localeCatalog_);
    Board::Storage::filesystem().remove(archivePath.c_str());
    if (!installed) {
        resetUpload();
        const int status = installed.error().contains("another pack") ? 409
                         : installed.error().contains("could not")    ? 500
                                                                       : 422;
        sendError(status, "invalid_locale_pack", installed.error(), "file");
        return;
    }

    const std::string id = std::move(*installed);
    changes_ |= Locales;
    statusLine1_ = "Locale installed";
    statusLine2_ = id.c_str();
    resetUpload();
    sendData(server_, jsonBuffer_, 201, api::IdResponse{id});
}

void CompanionSyncManager::handleLocaleUpload() {
    HTTPUpload& upload = server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (uploadFile_)
            uploadFile_.close();
        uploadError_.clear();
        uploadTmpPath_.clear();
        uploadFinalPath_.clear();

        if (!StorageFiles::ensureDirectory(StoragePaths::kLocalesPath)) {
            uploadError_ = "Locale-pack folder is unavailable";
            return;
        }
        uploadTmpPath_ = std::string{StoragePaths::kLocalesPath} + "/.upload.zip";
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadFile_ = Board::Storage::filesystem().open(uploadTmpPath_.c_str(), FILE_WRITE);
        if (!uploadFile_) {
            uploadError_ = "Could not create locale-pack upload";
            return;
        }
        statusLine1_ = "Receiving locale pack";
        statusLine2_ = upload.filename.c_str();
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!uploadError_.empty() || !uploadFile_)
            return;
        if (static_cast<size_t>(upload.totalSize) + upload.currentSize > kMaxLocalePackUploadBytes) {
            uploadError_ = "Locale-pack ZIP is too large";
            uploadFile_.close();
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
            return;
        }
        if (uploadFile_.write(upload.buf, upload.currentSize) != upload.currentSize) {
            uploadError_ = "Locale-pack ZIP write failed";
            uploadFile_.close();
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile_)
            uploadFile_.close();
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile_)
            uploadFile_.close();
        if (!uploadTmpPath_.empty())
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadError_ = "Locale-pack upload aborted";
    }
}

void CompanionSyncManager::handleLocaleDelete() {
    const String id = server_.pathArg(0);
    auto removed = locales::removeInstalled(Board::Storage::filesystem(), localeCatalog_,
                                             {id.c_str(), id.length()});
    if (!removed) {
        sendError(removed.error() == "invalid pack ID" ? 400 : 500, "remove_failed", removed.error(), "id");
        return;
    }
    changes_ |= Locales;
    statusLine1_ = "Locale removed";
    statusLine2_ = id.c_str();
    sendData(server_, jsonBuffer_, 200, api::DeleteResponse{std::string{id.c_str()}, true});
}

void CompanionSyncManager::handleFontUpload() {
    HTTPUpload& upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        const String familyArg = server_.arg("family");
        std::string family = sanitizeFilename({familyArg.c_str(), familyArg.length()});
        if (family.empty()) {
            family = sanitizeFilename({upload.filename.c_str(), upload.filename.length()});
            const size_t dot = family.rfind('.');
            if (dot != std::string::npos && dot > 0) {
                family.erase(dot);
            }
        }
        if (family.empty()) {
            uploadError_ = "Missing font family";
            return;
        }

        const std::string familyPath = std::string{StoragePaths::kFontsPath} + "/" + family;
        if (!StorageFiles::ensureDirectory(StoragePaths::kFontsPath)
            || !StorageFiles::ensureDirectory(familyPath.c_str())) {
            uploadError_ = "Fonts folder unavailable";
            return;
        }

        uploadFinalPath_ = familyPath + "/" + RFont4::kFilename;
        if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
            uploadError_ = "Font asset already exists";
            return;
        }
        uploadTmpPath_ = uploadFinalPath_ + ".tmp";
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadFile_ = Board::Storage::filesystem().open(uploadTmpPath_.c_str(), FILE_WRITE);
        if (!uploadFile_) {
            uploadError_ = "Could not create file";
            return;
        }
        uploadError_ = "";
        statusLine1_ = "Receiving font";
        statusLine2_ = family.c_str();
        ESP_LOGI("sync", "font upload start %s", uploadFinalPath_.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!uploadError_.empty() || !uploadFile_) {
            return;
        }
        if (static_cast<size_t>(upload.totalSize) + static_cast<size_t>(upload.currentSize) > kMaxFontUploadBytes) {
            uploadError_ = "Font file too large";
            uploadFile_.close();
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
            return;
        }
        const size_t written = uploadFile_.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            uploadError_ = "Font write failed";
            uploadFile_.close();
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
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
        if (!uploadTmpPath_.empty()) {
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        }
        uploadError_ = "Upload aborted";
        finishUpload(false);
    }
}

void CompanionSyncManager::handleBookDelete() {
    std::string path;
    const String id = server_.arg("id");
    if (id.isEmpty()) {
        sendError(400, "missing_field", "Book id is required", "id");
        return;
    }
    if (!resolveBookId({id.c_str(), id.length()}, path)) {
        sendError(404, "book_not_found", "Book not found", "id");
        return;
    }

    if (!Board::Storage::filesystem().remove(path.c_str())) {
        sendError(500, "storage_error", "Book could not be deleted");
        return;
    }

    statusLine1_ = "Book deleted";
    statusLine2_ = relativeLibraryName(path).c_str();
    ESP_LOGD("sync", "deleted %s", path.c_str());
    sendData(server_, jsonBuffer_, 200, api::DeleteResponse{std::string{id.c_str(), id.length()}, true});
}

void CompanionSyncManager::handleBookPosition() {
    const String body = server_.arg("plain");
    if (body.length() > 512) {
        sendError(413, "payload_too_large", "Position payload exceeds 512 bytes");
        return;
    }

    auto update = api::decode<api::BookPositionUpdate>({body.c_str(), body.length()});
    if (!update) {
        sendError(400, "invalid_json", update.error().c_str());
        return;
    }
    if (!update->id || !update->wordIndex) {
        sendError(400, "missing_field", "Book id and wordIndex are required");
        return;
    }

    std::string path;
    if (!resolveBookId(*update->id, path)) {
        sendError(404, "book_not_found", "Book not found", "id");
        return;
    }

    BookMetadata metadata;
    IndexedBookStore::Header header;
    if (!IndexedBook::readMetadata({path.c_str(), path.length()}, metadata, &header)) {
        sendError(409, "index_unavailable", "Book must be indexed on the reader before changing position");
        return;
    }
    if (header.wordCount == 0) {
        sendError(409, "empty_book", "Book has no readable words");
        return;
    }

    const uint32_t wordIndex = std::min<uint32_t>(*update->wordIndex, header.wordCount - 1);
    auto written =
        ReadingProgress::writeBookStatePosition({path.c_str(), path.length()},
                                                {header.sourceSize, header.sourceFingerprint, header.wordCount},
                                                wordIndex);
    if (!written) {
        Logger::failure("sync", "save reading position",
                        StoragePaths::bookStatePathFor({path.c_str(), path.length()}).c_str(), written.error());
        sendError(500, "storage_error", "Reading position could not be saved");
        return;
    }

    // TODO(reading-session): Notify the active in-memory book/session, if any.
    // The sidecar is authoritative here; CompanionSyncManager does not maintain
    // a second per-book position cache.
    statusLine1_ = "Position saved";
    statusLine2_ = relativeLibraryName(path).c_str();
    sendData(server_, jsonBuffer_, 200,
             api::BookPositionResponse{*update->id, wordIndex, ReadingProgress::percent(wordIndex, header.wordCount)});
}

void CompanionSyncManager::handleBookLanguageFonts() {
    const String body = server_.arg("plain");
    if (body.length() > 2048) {
        sendError(413, "payload_too_large", "Language font payload exceeds 2 KiB");
        return;
    }
    auto update = api::decode<api::BookLanguageFontsUpdate>({body.c_str(), body.length()});
    if (!update) {
        sendError(400, "invalid_json", update.error().c_str());
        return;
    }
    if (!update->id) {
        sendError(400, "missing_field", "Book id is required", "id");
        return;
    }
    std::string path;
    if (!resolveBookId(*update->id, path)) {
        sendError(404, "book_not_found", "Book not found", "id");
        return;
    }
    BookMetadata metadata;
    IndexedBookStore::Header header;
    if (!IndexedBook::readMetadata({path.c_str(), path.length()}, metadata, &header) || header.wordCount == 0) {
        sendError(409, "index_unavailable", "Book must be indexed before configuring language fonts");
        return;
    }

    std::vector<std::string_view> bookLanguages;
    bookLanguages.reserve(metadata.textRuns.size() + 1);
    const auto addLanguage = [&](std::string_view locale) {
        if (!locale.empty() && std::ranges::find(bookLanguages, locale) == bookLanguages.end())
            bookLanguages.push_back(locale);
    };
    addLanguage(metadata.locale);
    for (const BookTextRun& run: metadata.textRuns)
        addLanguage(run.locale);

    for (size_t index = 0; index < update->languageFonts.size(); ++index) {
        auto& selection = update->languageFonts[index];
        const auto preceding = std::span{update->languageFonts}.first(index);
        if (selection.locale == settings::kMathFontTarget) {
            if ((metadata.scriptMask & UnicodeText::ScriptMath) == 0) {
                sendError(422, "invalid_math", "Math is not present in this book", "languageFonts");
                return;
            }
            if (std::ranges::find(preceding, settings::kMathFontTarget,
                                  &settings::LanguageFont::locale) != preceding.end()) {
                sendError(422, "duplicate_math", "Math can select one font", "languageFonts");
                return;
            }
            const auto family = fontCatalog_.find(selection.fontId);
            if (!family || !family->get().supports(UnicodeText::ScriptMath)) {
                sendError(422, "incompatible_font", "Font does not support Math", "languageFonts");
                return;
            }
            selection.fontId = family->get().id;
            continue;
        }

        auto locale = LocaleTag::normalize(selection.locale);
        if (!locale || std::ranges::find(bookLanguages, *locale) == bookLanguages.end()) {
            sendError(422, "invalid_language", "Language is not present in this book", "languageFonts");
            return;
        }
        selection.locale = std::move(*locale);
        if (std::ranges::find(preceding, selection.locale, &settings::LanguageFont::locale) != preceding.end()) {
            sendError(422, "duplicate_language", "Each language can select one font", "languageFonts");
            return;
        }
        const auto family = fontCatalog_.find(selection.fontId);
        const uint32_t requiredScripts = metadata.scriptsForLocale(selection.locale) & ~UnicodeText::ScriptMath;
        if (requiredScripts == 0 || !family || !family->get().usableFor(selection.locale, requiredScripts)) {
            sendError(422, "incompatible_font", "Font does not support this language", "languageFonts");
            return;
        }
        selection.fontId = family->get().id;
    }

    auto written = ReadingProgress::writeBookLanguageFonts(
        path, {header.sourceSize, header.sourceFingerprint, header.wordCount}, std::move(update->languageFonts));
    if (!written) {
        Logger::failure("sync", "save language fonts", StoragePaths::bookStatePathFor(path).c_str(), written.error());
        sendError(500, "storage_error", "Language font choices could not be saved");
        return;
    }
    statusLine1_ = "Language fonts saved";
    statusLine2_ = relativeLibraryName(path).c_str();
    sendData(server_, jsonBuffer_, 200, api::IdResponse{*update->id});
}

void CompanionSyncManager::handleBookUpload() {
    HTTPUpload& upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        const String nameArg = server_.arg("name");
        std::string filename = sanitizeFilename({nameArg.c_str(), nameArg.length()});
        if (filename.empty()) {
            filename = sanitizeFilename({upload.filename.c_str(), upload.filename.length()});
        }
        if (filename.empty()) {
            uploadError_ = "Missing filename";
            return;
        }

        std::string lowered = filename;
        std::ranges::transform(lowered, lowered.begin(), AsciiText::toLower);
        if (!isSupportedBookName(lowered)) {
            filename += ".rsvp";
        }

        const String categoryArg = server_.arg("category");
        std::string category{categoryArg.c_str(), categoryArg.length()};
        std::ranges::transform(category, category.begin(), AsciiText::toLower);
        const char* targetDirectory =
            category == "article" ? StoragePaths::kArticleFilesPath : StoragePaths::kBookFilesPath;

        if (!StorageFiles::ensureDirectory(StoragePaths::kBooksPath)
            || !StorageFiles::ensureDirectory(StoragePaths::kBookFilesPath)
            || !StorageFiles::ensureDirectory(StoragePaths::kArticleFilesPath)) {
            uploadError_ = "Library folders unavailable";
            return;
        }
        uploadFinalPath_ = std::string{targetDirectory} + "/" + filename;
        uploadTmpPath_ = uploadFinalPath_ + ".tmp";
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadFile_ = Board::Storage::filesystem().open(uploadTmpPath_.c_str(), FILE_WRITE);
        if (!uploadFile_) {
            uploadError_ = "Could not create file";
            return;
        }
        uploadError_ = "";
        statusLine1_ = "Receiving book";
        statusLine2_ = filename.c_str();
        ESP_LOGI("sync", "upload start %s", uploadFinalPath_.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!uploadError_.empty() || !uploadFile_) {
            return;
        }
        const size_t written = uploadFile_.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            uploadError_ = "Write failed";
        }
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        ESP_LOGD("sync", "upload end bytes=%u error=%s", upload.totalSize, uploadError_.c_str());
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
        const String nameArg = server_.arg("name");
        std::string filename = sanitizeFilename({nameArg.c_str(), nameArg.length()});
        if (filename.empty()) {
            filename = sanitizeFilename({upload.filename.c_str(), upload.filename.length()});
        }
        if (filename.empty()) {
            uploadError_ = "Missing filename";
            return;
        }
        if (!ui::themes::hasThemeExtension({filename.c_str(), filename.length()})) {
            filename += ui::themes::kThemeExtension.data();
        }
        if (!StorageFiles::ensureDirectory(StoragePaths::kThemesPath)) {
            uploadError_ = "Themes folder unavailable";
            return;
        }

        uploadFinalPath_ = std::string{StoragePaths::kThemesPath} + "/" + filename;
        if (StorageFiles::fileExists(uploadFinalPath_.c_str())) {
            uploadError_ = "Theme already exists";
            return;
        }
        uploadTmpPath_ = uploadFinalPath_ + ".tmp";
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        uploadFile_ = Board::Storage::filesystem().open(uploadTmpPath_.c_str(), FILE_WRITE);
        if (!uploadFile_) {
            uploadError_ = "Could not create file";
            return;
        }
        uploadError_ = "";
        statusLine1_ = "Receiving theme";
        statusLine2_ = filename.c_str();
        ESP_LOGI("sync", "theme upload start %s", uploadFinalPath_.c_str());
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!uploadError_.empty() || !uploadFile_) {
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

void CompanionSyncManager::sendError(int status, const char* code, std::string_view message, const char* field) {
    api::ApiError error{code == nullptr ? "unknown" : code, std::string{message}, std::nullopt};
    if (field != nullptr && *field != '\0')
        error.field = field;
    if (auto encoded = api::encodeError(std::move(error), jsonBuffer_); !encoded) {
        ESP_LOGE("sync", "error response encode failed: %s", encoded.error().c_str());
        jsonBuffer_ =
            "{\"error\":{\"code\":\"serialization_failed\",\"message\":\"Error response could not be encoded\"}}";
        status = 500;
    }
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(status, "application/json", jsonBuffer_.c_str());
}

std::string CompanionSyncManager::deviceSuffix() const {
    uint64_t mac = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06X", static_cast<unsigned int>(mac & 0xFFFFFF));
    return suffix;
}

std::string CompanionSyncManager::sanitizeFilename(std::string_view name) const {
    std::string sanitized;
    sanitized.reserve(name.length());
    std::ranges::transform(name, std::back_inserter(sanitized), [](char c) {
        return isSafeFilenameChar(c) ? c : '-';
    });
    sanitized = std::string{AsciiText::trim(sanitized)};
    while (sanitized.starts_with('.')) {
        sanitized.erase(0, 1);
    }
    return sanitized;
}

CompanionSyncManager::RsvpMetadata CompanionSyncManager::readRsvpMetadata(std::string_view path) const {
    RsvpMetadata metadata;
    std::string loweredPath{path};
    std::ranges::transform(loweredPath, loweredPath.begin(), AsciiText::toLower);
    if (!loweredPath.ends_with(".rsvp")) {
        return metadata;
    }

    const std::string ownedPath{path};
    File file = Board::Storage::filesystem().open(ownedPath.c_str());
    if (!file || file.isDirectory()) {
        if (file) {
            file.close();
        }
        return metadata;
    }

    std::string line;
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
                line.clear();
                break;
            }
            continue;
        }

        if (metadata.title.empty()) {
            metadata.title = rsvpMetadataValueFromLine(line, "@title", pastDirectives);
        }
        if (metadata.author.empty() && !pastDirectives) {
            metadata.author = rsvpMetadataValueFromLine(line, "@author", pastDirectives);
        }
        if (!metadata.title.empty() && !metadata.author.empty()) {
            break;
        }

        if (pastDirectives) {
            break;
        }
        line.clear();
    }

    if (!line.empty() && !pastDirectives) {
        if (metadata.title.empty()) {
            metadata.title = rsvpMetadataValueFromLine(line, "@title", pastDirectives);
        }
        if (metadata.author.empty() && !pastDirectives) {
            metadata.author = rsvpMetadataValueFromLine(line, "@author", pastDirectives);
        }
    }

    file.close();
    return metadata;
}

bool CompanionSyncManager::progressForPath(std::string_view path, uint32_t sourceSize, uint32_t sourceFingerprint,
                                           uint32_t wordCount, uint32_t& wordIndex, uint8_t& percent,
                                           settings::ReadingOverrides& overrides) {
    if (wordCount <= 1) {
        return false;
    }

    const auto state = ReadingProgress::readBookState(path, {sourceSize, sourceFingerprint, wordCount});
    if (state) {
        wordIndex = state->wordIndex;
        percent = ReadingProgress::percent(wordIndex, wordCount);
        overrides = state->overrides;
        return true;
    }

    // TODO(reading-session): If the active book can have newer unsaved in-memory
    // progress, query that session here before reporting zero progress.
    return false;
}

std::string CompanionSyncManager::bookIdForPath(std::string_view path) const {
    uint32_t hash = 2166136261UL;
    for (const char c: path) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619UL;
    }
    char id[10];
    std::snprintf(id, sizeof(id), "b%08lx", static_cast<unsigned long>(hash));
    return id;
}

bool CompanionSyncManager::resolveBookId(std::string_view id, std::string& path) const {
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
                const std::string name = displayNameForPath(entry.name());
                std::string lowered = name;
                std::ranges::transform(lowered, lowered.begin(), AsciiText::toLower);
                if (isSupportedBookName(lowered)) {
                    const std::string candidate = std::string{directoryPath} + "/" + name;
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

    if (uploadTmpPath_.empty()) {
        return;
    }

    if (success && uploadError_.empty()) {
        auto replaced = replaceUploadedFile(uploadTmpPath_, uploadFinalPath_);
        if (!replaced) {
            Logger::failure("sync", "install book", uploadTmpPath_.c_str(), uploadFinalPath_.c_str(), replaced.error());
            uploadError_ = "Rename failed";
            Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
        } else {
            statusLine1_ = "Book received";
            statusLine2_ = uploadFinalPath_.c_str();
            ESP_LOGI("sync", "upload ready %s", uploadFinalPath_.c_str());
        }
    } else {
        Board::Storage::filesystem().remove(uploadTmpPath_.c_str());
    }

    uploadTmpPath_ = "";
}
