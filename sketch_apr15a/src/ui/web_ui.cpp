#include "include/ui/web_ui.h"

// Web 控制台前端页面
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
    <title>Cody 控制面板</title>
    <style>
        *{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
        body{background:#121214;color:#fff;font-family:-apple-system,Arial,sans-serif;padding:16px;padding-bottom:60px;}
        .box{background:#1c1c1f;border-radius:16px;max-width:400px;margin:0 auto}
        .tabs{display:flex;background:#222;border-bottom:1px solid #333;border-radius:16px 16px 0 0;overflow:hidden;}
        .tab-btn{flex:1;padding:14px 4px;text-align:center;cursor:pointer;color:#888;font-size:14px;border:none;background:none;transition:0.2s}
        .tab-btn.active{color:#e67e22;border-bottom:2px solid #e67e22;background:#2a2a2f}
        .tab-content{padding:16px;display:none;animation:fadeIn 0.3s ease}
        .tab-content.active{display:block}
        @keyframes fadeIn { from {opacity:0; transform:translateY(5px)} to {opacity:1; transform:translateY(0)} }
        .btn{background:#e67e22;color:#fff;border:none;padding:12px;border-radius:10px;width:100%;margin-top:8px;font-weight:bold;font-size:14px;cursor:pointer;transition:0.2s}
        .btn:active{transform:scale(0.98);opacity:0.9}
        .btn.small{padding:8px;font-size:12px}
        .btn.danger{background:#e74c3c}
        .btn.success{background:#2ecc71}
        .btn.warning{background:#f39c12}
        .mode-group{display:flex;flex-direction:column;gap:12px;margin-bottom:16px}
        .mode-btn{flex:1;padding:18px;background:#2a2a2f;border:2px solid #444;border-radius:12px;cursor:pointer;transition:0.2s}
        .mode-btn .mode-name{font-size:18px;margin-bottom:4px;font-weight:bold}
        .mode-btn.img-mode.active{background:linear-gradient(135deg,#e67e22,#d35400);color:#fff;border-color:#e67e22}
        .mode-btn.clock-mode.active{background:linear-gradient(135deg,#3498db,#2980b9);color:#fff;border-color:#3498db}
        .mode-btn.note-mode.active{background:linear-gradient(135deg,#f1c40f,#f39c12);color:#fff;border-color:#f1c40f}
        .mode-btn.expr-mode.active{background:linear-gradient(135deg,#9b59b6,#8e44ad);color:#fff;border-color:#9b59b6}
        .info-text{background:#2a2a2f;padding:12px;border-radius:10px;font-size:14px;margin-top:15px;text-align:center;color:#ccc}
        .slot-grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin:15px 0}
        .slot-card{background:#2a2a2f;border-radius:12px;padding:12px;text-align:center;display:flex;flex-direction:column;}
        .slot-card.has-image{background:#222d28}
        .thumb-canvas{position:relative;width:100%;max-width:140px;aspect-ratio:1/1;margin:0 auto 10px;border-radius:8px;background:#111;display:flex;align-items:center;justify-content:center;color:#555;font-size:30px;cursor:pointer;}
        .slot-number{font-size:14px;font-weight:bold;margin-bottom:10px;color:#eee}
        .slot-actions{display:flex;flex-direction:column;margin-top:auto}
        .slot-actions button{padding:10px;font-size:13px;border:none;border-radius:8px;cursor:pointer;width:100%;font-weight:bold;transition:0.2s;background:#3498db;color:#fff;}
        .slot-actions button:active{transform:scale(0.95)}
        
        .delete-x {
            position: absolute; top: -8px; right: -8px; background: #e74c3c; color: #fff; border: 2px solid #1c1c1f; border-radius: 50%;
            width: 24px; height: 24px; font-size: 14px; line-height: 20px; text-align: center; cursor: pointer; z-index: 10; padding: 0; font-weight: bold; transition: 0.2s;
        }
        .delete-x:active { transform: scale(0.85); }
        
        .slider-container{background:#2a2a2f;padding:16px;border-radius:10px;margin:15px 0}
        .toggle-switch{display:flex;align-items:center;justify-content:space-between;margin:10px 0}
        .toggle-switch input{width:48px;height:24px;-webkit-appearance:none;background:#555;border-radius:12px;position:relative;cursor:pointer;outline:none}
        .toggle-switch input:checked{background:#e67e22}
        .toggle-switch input:before{content:'';position:absolute;width:20px;height:20px;background:#fff;border-radius:50%;top:2px;left:2px;transition:0.2s}
        .toggle-switch input:checked:before{left:26px}
        
        input[type=range] { -webkit-appearance: none; width: 100%; background: transparent; }
        input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; height: 18px; width: 18px; border-radius: 50%; background: #e67e22; cursor: pointer; margin-top: -6px; box-shadow: 0 0 5px rgba(0,0,0,0.5); }
        input[type=range]::-webkit-slider-runnable-track { width: 100%; height: 6px; cursor: pointer; background: #444; border-radius: 3px; }
        
        .p-bar{width:100%;background:#111;height:10px;border-radius:5px;overflow:hidden;border:1px solid #333}
        #fill{width:0;height:100%;background:linear-gradient(90deg,#e67e22,#f1c40f);border-radius:5px;transition:width 0.1s linear}
        #st{margin-top:8px;color:#aaa;text-align:center;font-size:13px}
        .fs-info{background:#2a2a2f;padding:12px;border-radius:10px;margin-bottom:10px;font-size:13px;color:#ddd}
        .space-bar{width:100%;height:8px;background:#333;border-radius:4px;margin-top:8px;overflow:hidden}
        .space-used{height:100%;background:#e67e22;border-radius:4px;transition:width 0.3s ease}
        .ver-box,.danger-zone,.warning-zone{background:#2a2a2f;padding:15px;border-radius:10px;margin-bottom:15px}
        .toast{position:fixed;bottom:-60px;left:50%;transform:translateX(-50%);background:#e67e22;color:#fff;padding:12px 24px;border-radius:30px;font-size:14px;font-weight:bold;transition:bottom 0.4s cubic-bezier(0.18,0.89,0.32,1.28);z-index:1000;box-shadow:0 4px 15px rgba(230,126,34,0.4);white-space:nowrap}
        .toast.show{bottom:30px;}
        .overlay{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.8);backdrop-filter:blur(3px);z-index:999;display:none;align-items:center;justify-content:center;opacity:0;transition:opacity 0.2s ease}
        .overlay.show{display:flex;opacity:1;}
        .modal{background:#232328;padding:24px;border-radius:16px;width:85%;max-width:320px;text-align:center;transform:scale(0.9);transition:transform 0.2s cubic-bezier(0.18,0.89,0.32,1.28);border:1px solid #333;box-shadow:0 10px 30px rgba(0,0,0,0.5)}
        .overlay.show .modal{transform:scale(1);}
        .modal-title{font-size:18px;font-weight:bold;margin-bottom:12px;color:#fff;}
        .modal-msg{font-size:14px;color:#bbb;margin-bottom:24px;line-height:1.5}
        .modal-btns{display:flex;gap:12px;}
        .modal-btn{flex:1;padding:12px;border:none;border-radius:10px;font-weight:bold;cursor:pointer;font-size:15px;transition:0.2s}
        .modal-btn.cancel{background:#3a3a40;color:#fff;}
        .modal-btn.confirm{background:#e67e22;color:#fff;}
        
        .note-input{width:100%;background:#1c1c1f;border:1px solid #444;color:#fff;padding:12px;border-radius:10px;margin-bottom:10px;resize:none;font-family:inherit;font-size:14px;}
        .note-input:focus{outline:none;border-color:#f1c40f;}
        .note-item{background:#2a2a2f;padding:15px;border-radius:10px;margin-bottom:12px;border-left:4px solid #444;transition:0.3s;}
        .note-item.pinned{border-left-color:#e74c3c; background:#342626;}
        .note-content{font-size:15px;margin-bottom:12px;word-break:break-all;line-height:1.5;}
        .note-footer{font-size:11px;color:#888;display:flex;justify-content:space-between;align-items:center;}
        .pin-badge{font-size:12px;color:#e74c3c;font-weight:bold;margin-bottom:8px;display:inline-block;}
    </style>
</head>
<body>
<div class="box">
    <div class="tabs">
        <button class="tab-btn active" data-tab="0">模式</button>
        <button class="tab-btn" data-tab="1">图库</button>
        <button class="tab-btn" data-tab="4">笔记</button>
        <button class="tab-btn" data-tab="3">设置</button>
    </div>
    
    <div class="tab-content active" id="tab0">
        <div class="mode-group">
            <div id="btnImgMode" class="mode-btn img-mode active" data-mode="0">
                <div class="mode-name">🖼️ 图片模式</div><div class="mode-desc" style="font-size:12px;color:#eee">显示图片 (支持多图轮播)</div>
            </div>
            <div id="btnClockMode" class="mode-btn clock-mode" data-mode="1">
                <div class="mode-name">⏰ 时钟模式</div><div class="mode-desc" style="font-size:12px;color:#eee">显示实时时间和日期</div>
            </div>
            <div id="btnNoteMode" class="mode-btn note-mode" data-mode="2">
                <div class="mode-name">📝 笔记模式</div><div class="mode-desc" style="font-size:12px;color:#eee">在屏幕上排版显示笔记</div>
            </div>
            <div id="btnExprMode" class="mode-btn expr-mode" data-mode="3">
                <div class="mode-name">😊 表情模式</div><div class="mode-desc" style="font-size:12px;color:#eee">随机播放可爱表情动画</div>
            </div>
        </div>
        <div class="info-text" id="modeInfo">当前: 图片模式</div>
    </div>
    
    <div class="tab-content" id="tab1">
        <canvas id="compressCanvas" width="240" height="240" style="display:none"></canvas>
        <input type="file" id="fileInput" accept="image/*" style="display:none">
        
        <div class="slider-container" style="margin-top:0; border:1px solid #444;">
            <div class="toggle-switch">
                <span style="font-weight:bold; color:#e67e22;">🎬 启用图片自动轮播</span>
                <input type="checkbox" id="slideshowToggle" disabled>
            </div>
            <div style="display:flex; align-items:center; margin-top:15px;">
                <span style="font-size:13px; margin-right:15px; color:#ccc; white-space:nowrap;">间隔: <span id="imgIntervalVal">10</span>秒</span>
                <input type="range" id="intervalInput" min="3" max="60" style="flex:1;">
                <button id="saveIntervalBtn" class="btn small" style="width:auto; margin-left:15px; margin-top:0; background:#3498db;">保存设置</button>
            </div>
        </div>

        <div id="uploadProgressArea" style="display:none; background:#2a2a2f; padding:20px; border-radius:12px; margin-bottom:15px; border:1px solid #444;">
            <div style="font-weight:bold;color:#f39c12;margin-bottom:10px;text-align:center;">🚀 正在写入闪存...</div>
            <div class="p-bar" id="progressBar" style="display:block;"><div id="fill"></div></div>
            <div id="st">0%</div>
        </div>
        
        <div class="slot-grid" id="slotGrid"></div>
    </div>

    <div class="tab-content" id="tab4">
        <div class="slider-container" style="margin-top:0; margin-bottom:20px; border:1px solid #444;">
            <div class="toggle-switch">
                <span style="font-weight:bold; color:#f1c40f;">🔂 启用笔记自动轮播</span>
                <input type="checkbox" id="noteSlideshowToggle">
            </div>
            <div style="display:flex; align-items:center; margin-top:15px;">
                <span style="font-size:13px; margin-right:15px; color:#ccc; white-space:nowrap;">间隔: <span id="noteIntervalVal">10</span>秒</span>
                <input type="range" id="noteIntervalInput" min="3" max="60" style="flex:1;">
                <button id="saveNoteIntervalBtn" class="btn small" style="width:auto; margin-left:15px; margin-top:0; background:#3498db;">保存设置</button>
            </div>
            <div style="font-size:11px; color:#888; margin-top:12px; line-height:1.4;">提示：若某条笔记被“置顶”，屏幕将强制显示该置顶笔记，轮播功能将暂时挂起。</div>
        </div>

        <div style="background:#2a2a2f; padding:15px; border-radius:10px; margin-bottom:20px;">
            <textarea id="noteText" class="note-input" rows="3" placeholder="输入笔记内容 (最多75字)..."></textarea>
            <div style="display:flex; justify-content:space-between; align-items:center;">
                <span id="charCount" style="font-size:12px; color:#888;">0/75</span>
                <div style="display:flex; gap:10px;">
                    <button id="cancelEditBtn" class="btn small" style="display:none; background:#555; width:auto; margin:0;">取消编辑</button>
                    <button id="saveNoteBtn" class="btn success" style="background:#f1c40f; color:#000; width:auto; padding:8px 16px; margin:0;">➕ 添加新笔记</button>
                </div>
            </div>
        </div>
        
        <h4 style="font-size:15px; color:#ddd; margin-bottom:10px; padding-left:4px;">历史笔记</h4>
        <div id="noteList"></div>
    </div>
    
    <div class="tab-content" id="tab3">
        <div class="fs-info" style="margin-bottom:15px; padding:15px; background:#2a2a2f; border-radius:12px;">
            <div style="display:flex;justify-content:space-between;font-weight:bold;margin-bottom:8px;">
                <span>📊 内部存储空间</span><span id="sysSpaceText">检查中...</span>
            </div>
            <div class="space-bar"><div class="space-used" id="sysSpaceBar" style="width:0%"></div></div>
            <div class="space-text" id="sysSpaceDetail" style="margin-top:8px; text-align:right;"></div>
        </div>

        <div class="ver-box" style="background:#2a2a2f; padding:15px; border-radius:12px; margin-bottom:15px;">
            <div style="display:flex; justify-content:space-between; align-items:center;">
                <span id="versionLabel" style="font-size:15px;font-weight:bold;color:#e67e22;">版本: 获取中...</span>
                <button id="checkUpdateBtn" class="btn small" style="width:auto; margin:0; background:#3498db;">检查更新</button>
            </div>
            <div id="updateInfo" style="display:none"></div>
        </div>

        <div style="background:#2a2a2f; padding:15px; border-radius:12px; margin-bottom:15px;">
            <label style="display:block;margin-bottom:15px;font-size:15px;font-weight:bold;">💡 屏幕背光亮度</label>
            <input type="range" id="brightness" min="0" max="255" value="255" style="width:100%; cursor:pointer;">
            <div style="display:flex;justify-content:space-between;margin-top:8px;font-size:12px;color:#888;">
                <span>暗</span><span>亮</span>
            </div>
        </div>

        <div class="danger-zone" style="background:#2a2a2f; padding:15px; border-radius:12px; margin-bottom:15px;">
            <h4 style="color:#e74c3c;margin-bottom:10px;font-size:15px;">⚠️ 危险操作</h4>
            <button id="formatFSBtn" class="btn danger" style="background:#c0392b;">🧹 深度格式化 (清空图片与笔记)</button>
        </div>
        
        <div class="warning-zone" style="background:#2a2a2f; padding:15px; border-radius:12px;">
            <h4 style="color:#f39c12;margin-bottom:10px;font-size:15px;">⚡ 系统维护</h4>
            <button id="resetSystemBtn" class="btn warning">🔄 恢复出厂设置 (清除WiFi配网等)</button>
        </div>
    </div>
</div>

<div id="toast" class="toast"></div>
<div id="overlay" class="overlay">
    <div class="modal">
        <div class="modal-title">系统提示</div>
        <div class="modal-msg" id="modalMsg">确定要执行此操作吗？</div>
        <div class="modal-btns">
            <button class="modal-btn cancel" id="modalCancel">取消</button>
            <button class="modal-btn confirm" id="modalConfirm">确定</button>
        </div>
    </div>
</div>

<script>
(function(){
    var mode=0, imgData=null, uploading=false, uploadY=0, targetUploadSlot=0;
    var loadedThumbs = [false,false,false]; 
    var editNoteIndex = -1;
    var noteCfg = {};
    
    function $(id){return document.getElementById(id);}
    
    $('noteText').addEventListener('input', function() {
        if (this.value.length > 75) {
            this.value = this.value.substring(0, 75);
            showToast('⚠️ 最多只能输入 75 个字');
        }
        $('charCount').innerText = this.value.length + '/75';
    });

    $('intervalInput').addEventListener('input', function() { $('imgIntervalVal').innerText = this.value; });
    $('noteIntervalInput').addEventListener('input', function() { $('noteIntervalVal').innerText = this.value; });

    var isUserActive = false;
    var uiActivityTimer = null;
    function markUserActive() {
        isUserActive = true; clearTimeout(uiActivityTimer);
        uiActivityTimer = setTimeout(function(){ isUserActive = false; processThumbQueue(); }, 1200); 
    }
    document.addEventListener('touchstart', markUserActive, {passive: true});
    document.addEventListener('mousemove', markUserActive, {passive: true});
    document.addEventListener('mousedown', markUserActive, {passive: true});
    document.addEventListener('keydown', markUserActive, {passive: true});

    var slotHasImage = [false,false,false];
    var thumbQueue = [];
    var isLoadingThumb = false;
    function requestThumb(slot) {
        if (slot < 0 || slot >= 3) return;
        if (!slotHasImage[slot]) return;
        if (loadedThumbs[slot]) return;
        var lt = $('loading-text-'+slot);
        if (lt) {
            lt.style.display = 'block';
            var pctEl = lt.querySelector('.pct'); if (pctEl) pctEl.innerText = '同步 0%';
        }
        var lb = $('loading-bar-'+slot); if (lb) lb.style.width = '0%';
        if (thumbQueue.indexOf(slot) === -1) thumbQueue.push(slot);
        processThumbQueue();
    }
    function processThumbQueue() {
        if(thumbQueue.length === 0 || isLoadingThumb) return;
        if(isUserActive) { setTimeout(processThumbQueue, 300); return; }
        
        isLoadingThumb = true; var slot = thumbQueue.shift();
        var loadTxt = $('loading-text-'+slot);
        var loadBar = $('loading-bar-'+slot);
        if (loadTxt) { loadTxt.style.display = 'block'; loadTxt.querySelector('.pct').innerText = '同步 0%'; }
        if (loadBar) loadBar.style.width = '0%';

        function fetchImageWithProgress(url, onProgress) {
            return fetch(url).then(async function(r) {
                if(!r.ok) throw new Error('Not found');

                // 优先使用流式读取来显示“同步进度”
                if (r.body && r.body.getReader) {
                    var reader = r.body.getReader();
                    var chunks = [];
                    var received = 0;
                    var total = parseInt(r.headers.get('Content-Length') || '0', 10);
                    if (!total || isNaN(total)) total = 115200; // RGB565 240*240*2

                    while (true) {
                        var res = await reader.read();
                        if (res.done) break;
                        chunks.push(res.value);
                        received += res.value.length;
                        if (onProgress) onProgress(received, total);
                    }
                    var out = new Uint8Array(received);
                    var offset = 0;
                    for (var i = 0; i < chunks.length; i++) {
                        out.set(chunks[i], offset);
                        offset += chunks[i].length;
                    }
                    return out.buffer;
                }

                // 兼容不支持流式读取的环境
                var buf = await r.arrayBuffer();
                if (onProgress) onProgress(buf.byteLength, buf.byteLength);
                return buf;
            });
        }

        fetchImageWithProgress('/get_image?slot=' + slot + '&t=' + Date.now(), function(received, total) {
            var pct = Math.max(0, Math.min(100, Math.round(received / total * 100)));
            if (loadTxt) loadTxt.querySelector('.pct').innerText = '同步 ' + pct + '%';
            if (loadBar) loadBar.style.width = pct + '%';
        })
        .then(function(buffer) {
            var canvas = $('thumb-'+slot); if(!canvas) return;
            var ctx = canvas.getContext('2d'), imgD = ctx.createImageData(240, 240), data = imgD.data;
            var src = new Uint8Array(buffer), j = 0;
            for(var i = 0; i < 115200; i += 2) {
                var p = src[i] | (src[i+1] << 8);
                data[j++] = ((p >> 11) & 0x1F) * 8; data[j++] = ((p >> 5) & 0x3F) * 4; data[j++] = (p & 0x1F) * 8; data[j++] = 255;
            }
            ctx.putImageData(imgD, 0, 0); loadedThumbs[slot] = true;
            if(loadTxt) loadTxt.style.display = 'none';
            canvas.style.display = 'block';
        })
        .catch(function(e) { console.log("Load err: " + slot); })
        .finally(function() { isLoadingThumb = false; setTimeout(processThumbQueue, 300); });
    }

    function qsa(sel){return document.querySelectorAll(sel);}
    var toastTimeout;
    function showToast(msg) {
        var t = $('toast'); t.innerHTML = msg; t.classList.add('show');
        clearTimeout(toastTimeout); toastTimeout = setTimeout(() => t.classList.remove('show'), 3000);
    }
    function showConfirm(msg, onConfirm) {
        $('modalMsg').innerHTML = msg; $('overlay').classList.add('show');
        $('modalCancel').onclick = () => $('overlay').classList.remove('show');
        $('modalConfirm').onclick = () => { $('overlay').classList.remove('show'); if(onConfirm) onConfirm(); };
    }
    
    function switchTab(i){
        qsa('.tab-content').forEach(t => t.classList.remove('active')); qsa('.tab-btn').forEach(b => b.classList.remove('active'));
        var tC = $('tab'+i); if(tC) tC.classList.add('active');
        var tB = document.querySelector('.tab-btn[data-tab="'+i+'"]'); if(tB) tB.classList.add('active');
        if(i==1) refreshStatus(); if(i==3) refreshSystemSpace(); if(i==4) refreshNotes(); 
    }
    qsa('.tab-btn').forEach(btn => btn.addEventListener('click', function(){ switchTab(parseInt(this.dataset.tab)); }));
    
    function setMode(m){
        fetch('/set_mode?mode='+m).then(r=>r.json()).then(d=>{
            if(d.status=='ok'){
                mode=m;
                $('btnImgMode').classList[m===0?'add':'remove']('active'); $('btnClockMode').classList[m===1?'add':'remove']('active'); $('btnNoteMode').classList[m===2?'add':'remove']('active'); $('btnExprMode').classList[m===3?'add':'remove']('active');
                if(m===0) $('modeInfo').innerHTML='当前状态: 运行图片显示';
                else if(m===1) $('modeInfo').innerHTML='当前状态: 显示实时时钟';
                else if(m===2) $('modeInfo').innerHTML='当前状态: 显示屏幕笔记';
                else if(m===3) $('modeInfo').innerHTML='当前状态: 随机表情动画';
            }
        });
    }
    $('btnImgMode').addEventListener('click',()=>setMode(0)); $('btnClockMode').addEventListener('click',()=>setMode(1)); $('btnNoteMode').addEventListener('click',()=>setMode(2)); $('btnExprMode').addEventListener('click',()=>setMode(3));
    
    window.clickSlotAction = function(i) {
        if(uploading){showToast('上传中请稍候');return;}
        if (loadedThumbs[i]) {
            fetch('/set_current?slot='+i).then(()=>{ showToast('图片已推送至屏幕'); });
            return;
        }
        if (slotHasImage[i]) {
            // 槽位已有图但缩略图未同步：点击触发同步，并显示进度条
            requestThumb(i);
            return;
        }
        // 槽位无图：点击进入上传
        targetUploadSlot = i; $('fileInput').click();
    };

    window.triggerFilePicker = function(i) {
        if(uploading){showToast('上传中请稍候');return;}
        targetUploadSlot = i; $('fileInput').click();
    };

    function initGrid() {
        var html = '';
        for(var i=0; i<3; i++) {
            html += `<div class="slot-card" id="slot-card-${i}">
                <div class="slot-number">槽位 ${i+1}</div>
                <div id="thumb-container-${i}" class="thumb-canvas" onclick="clickSlotAction(${i})">
                    <button id="btn-del-${i}" class="delete-x" style="display:none;" onclick="event.stopPropagation(); deleteSlot(${i})">×</button>
                    <canvas id="thumb-${i}" width="240" height="240" style="display:none;width:100%;height:100%;border-radius:8px;"></canvas>
                    <span id="empty-icon-${i}">🈳</span>
                    <div id="loading-text-${i}" style="display:none;width:82%;max-width:140px;">
                        <div class="pct" style="font-size:12px;color:#f39c12;font-weight:bold;">同步 0%</div>
                        <div style="width:100%;height:6px;background:#333;border-radius:999px;overflow:hidden;margin-top:6px;">
                            <div id="loading-bar-${i}" style="width:0%;height:100%;background:linear-gradient(90deg,#3498db,#2ecc71);"></div>
                        </div>
                    </div>
                </div>
                <div class="slot-actions">
                    <button id="btn-upload-${i}" onclick="triggerFilePicker(${i})">📁 选择图片</button>
                </div></div>`;
        }
        $('slotGrid').innerHTML = html;
    }
    initGrid();
    
    window.deleteSlot = function(i) {
        if(uploading){showToast('上传中请稍候');return;}
        showConfirm('确定删除槽位 '+(i+1)+' 的图片吗？', ()=>{ 
            fetch('/delete_image?slot='+i).then(()=>{ loadedThumbs[i]=false; refreshStatus(); refreshSystemSpace(); showToast('已删除'); }); 
        });
    }

    function refreshStatus(){
        fetch('/image_info').then(r=>r.json()).then(d=>{
            for(var i=0; i<3; i++) {
                var isHas = d.slots[i];
                slotHasImage[i] = !!isHas;
                var card = $('slot-card-'+i);
                card.className = 'slot-card ' + (isHas ? 'has-image ' : '');
                
                if(isHas) {
                    $('empty-icon-'+i).style.display = 'none'; 
                    $('thumb-container-'+i).style.background = '#000';
                    if(!loadedThumbs[i]) {
                        $('thumb-'+i).style.display = 'none';
                        // 默认不自动拉取缩略图，避免进入图库页就占满带宽/CPU导致“加载很慢”
                        var lt = $('loading-text-'+i);
                        if (lt) {
                            lt.style.display = 'block';
                            var pctEl = lt.querySelector('.pct'); if (pctEl) pctEl.innerText = '点击同步';
                        }
                        var lb = $('loading-bar-'+i); if (lb) lb.style.width = '0%';
                    } else {
                        $('thumb-'+i).style.display = 'block';
                        $('loading-text-'+i).style.display = 'none';
                    }
                } else {
                    $('thumb-'+i).style.display = 'none'; 
                    $('loading-text-'+i).style.display = 'none';
                    $('empty-icon-'+i).style.display = 'block'; 
                    $('thumb-container-'+i).style.background = '#111'; 
                    loadedThumbs[i] = false;
                }
                
                $('btn-del-'+i).style.display = isHas ? 'block' : 'none';
                var upBtn = $('btn-upload-'+i);
                if(isHas) { upBtn.style.background = '#e67e22'; upBtn.innerHTML = '🔄 替换图片'; } 
                else { upBtn.style.background = '#3498db'; upBtn.innerHTML = '📁 选择图片'; }
            }
            // 不再自动 processThumbQueue()；改为用户点击时才同步
            fetch('/slideshow_config').then(r=>r.json()).then(c=>{
                $('slideshowToggle').disabled = false; $('slideshowToggle').checked = c.enabled; 
                $('intervalInput').value = c.interval; $('imgIntervalVal').innerText = c.interval;
            });
        });
    }
    
    function refreshSystemSpace(){
        fetch('/fs_space').then(r=>r.json()).then(d=>{
            var pct=(d.used/d.total*100).toFixed(1);
            $('sysSpaceText').innerHTML=d.used+' KB / '+d.total+' KB';
            $('sysSpaceBar').style.width=pct+'%'; $('sysSpaceDetail').innerHTML='可用: '+d.free+' KB ('+(100-pct).toFixed(1)+'%)';
        });
    }

    function refreshNotes() {
        fetch('/note_config').then(r=>r.json()).then(cfg => {
            noteCfg = cfg;
            $('noteSlideshowToggle').checked = cfg.slideshow;
            $('noteIntervalInput').value = cfg.interval; $('noteIntervalVal').innerText = cfg.interval;
            return fetch('/get_notes');
        }).then(r=>r.json()).then(notes => {
            window.noteDataCache = notes;
            var pinnedNote = null;
            var unpinnedNotes = [];
            
            for(var i=0; i<notes.length; i++) {
                notes[i].realIdx = i;
                if(i == noteCfg.pinned) {
                    pinnedNote = notes[i];
                } else {
                    unpinnedNotes.unshift(notes[i]);
                }
            }
            
            var sortedNotes = [];
            if (pinnedNote) sortedNotes.push(pinnedNote);
            sortedNotes = sortedNotes.concat(unpinnedNotes);

            var html = '';
            sortedNotes.forEach(function(note) {
                var realIdx = note.realIdx;
                var isPinned = (realIdx == noteCfg.pinned);
                html += `<div class="note-item ${isPinned ? 'pinned' : ''}">`;
                if(isPinned) html += `<div class="pin-badge">📌 当前置顶</div>`;
                html += `<div class="note-content">${note.content}</div>`;
                html += `<div class="note-footer">`;
                html += `<span>⏳ ${note.time}</span>`;
                html += `<div style="display:flex; gap:8px;">`;
                html += `<button class="btn small" style="background:#34495e;margin:0" onclick="startEditNote(${realIdx})">编辑</button>`;
                html += `<button class="btn small" style="background:${isPinned ? '#555' : '#e74c3c'};margin:0" onclick="togglePin(${realIdx}, ${isPinned})">${isPinned ? '取消置顶' : '置顶'}</button>`;
                html += `<button class="btn small danger" style="margin:0" onclick="deleteNote(${realIdx})">删除</button>`;
                html += `</div></div></div>`;
            });
            $('noteList').innerHTML = html || '<p style="text-align:center;color:#666;font-size:14px;padding:20px;">暂无历史笔记</p>';
        });
    }

    $('saveNoteBtn').addEventListener('click', function() {
        var content = $('noteText').value.trim();
        if(!content) { showToast('请输入内容'); return; }
        var url = '/save_note?content=' + encodeURIComponent(content);
        if(editNoteIndex !== -1) url += '&index=' + editNoteIndex;
        fetch(url).then(r=>r.json()).then(res=>{
            if(res.status == 'ok') { cancelEdit(); refreshNotes(); showToast(editNoteIndex !== -1 ? '修改已保存' : '新笔记已添加'); } else showToast('保存失败 (可能超限制)');
        });
    });

    window.startEditNote = function(idx) {
        var note = window.noteDataCache[idx]; if(!note) return;
        editNoteIndex = idx; $('noteText').value = note.content; 
        $('charCount').innerText = note.content.length + '/75';
        $('saveNoteBtn').innerHTML = '💾 保存修改'; $('saveNoteBtn').style.background = '#2ecc71'; $('cancelEditBtn').style.display = 'block'; $('noteText').focus(); window.scrollTo({top: 0, behavior: 'smooth'});
    };
    window.cancelEdit = function() {
        editNoteIndex = -1; $('noteText').value = ''; 
        $('charCount').innerText = '0/75';
        $('saveNoteBtn').innerHTML = '➕ 添加新笔记'; $('saveNoteBtn').style.background = '#f1c40f'; $('cancelEditBtn').style.display = 'none';
    };
    
    window.deleteNote = function(idx) {
        showConfirm('确定永久删除这条笔记吗？', function(){ fetch('/delete_note?index=' + idx).then(r=>r.json()).then(function(){ if(editNoteIndex === idx) cancelEdit(); refreshNotes(); showToast('已删除'); }); });
    };
    window.togglePin = function(idx, isPinned) {
        var target = isPinned ? -1 : idx;
        fetch('/set_note_config?pinned=' + target).then(r=>r.json()).then(res=>{ refreshNotes(); showToast(isPinned ? '已取消屏幕置顶' : '已将该笔记固定到屏幕'); });
    };

    $('noteSlideshowToggle').addEventListener('change', function() { fetch('/set_note_config?slideshow=' + this.checked).then(r=>r.json()).then(res=>{ showToast(this.checked ? '已开启自动轮播' : '已暂停轮播'); }); });
    $('saveNoteIntervalBtn').addEventListener('click', function() { fetch('/set_note_config?interval=' + $('noteIntervalInput').value).then(r=>r.json()).then(res=>{ showToast('✅ 轮播间隔已更新'); }); });
    
    $('fileInput').addEventListener('change',function(e){
        var f=e.target.files[0]; if(!f)return; var r=new FileReader();
        r.onload=function(ev){
            var img=new Image();
            img.onload=function(){
                var c=$('compressCanvas'),ctx=c.getContext('2d'), s=Math.min(img.width,img.height);
                ctx.drawImage(img,(img.width-s)/2,(img.height-s)/2,s,s,0,0,240,240);
                imgData=ctx.getImageData(0,0,240,240); 
                doUploadImage();
            }; img.src=ev.target.result;
        }; r.readAsDataURL(f);
    });
    
    function doUploadImage(){
        if(!imgData||uploading)return; var d=imgData.data; uploading=true;uploadY=0;
        $('uploadProgressArea').style.display='block'; $('st').innerHTML='正在清空旧数据...';
        
        fetch('/upload_start?slot='+targetUploadSlot).then(r=>r.json()).then(res=>{
            if(res.status!='ok'){showToast('初始化失败');resetUpload();return;} sendChunk();
        }).catch(function(){showToast('网络通信异常');resetUpload();});
        
        function sendChunk(){
            if(uploadY>=240){
                $('fill').style.width='100%';$('st').innerHTML='数据组装中...';
                fetch('/upload_finish').then(r=>r.json()).then(res=>{
                    resetUpload();
                    if(res.status=='ok'){
                        imgData=null; $('fileInput').value=''; showToast('🎉 上传成功');
                        loadedThumbs[targetUploadSlot] = false; refreshStatus(); refreshSystemSpace();
                    }else showToast('保存失败'); 
                }); return;
            }
            var h=Math.min(10,240-uploadY),hex='';
            for(var y=0;y<h;y++){
                for(var x=0;x<240;x++){
                    var i=((uploadY+y)*240+x)*4, c=((d[i]&0xF8)<<8)|((d[i+1]&0xFC)<<3)|(d[i+2]>>3);
                    hex+=c.toString(16).padStart(4,'0');
                }
            }
            fetch('/upload_chunk?y='+uploadY+'&h='+h,{method:'POST',body:hex}).then(r=>r.json()).then(res=>{
                if(res.status!='ok'){showToast('传输失败');resetUpload();return;}
                uploadY+=h; var pct = Math.round(uploadY/240*100); $('fill').style.width=pct+'%'; $('st').innerHTML=pct+'%'; sendChunk();
            }).catch(function(){showToast('传输中断重试');resetUpload();});
        }
    }
    
    function resetUpload(){ uploading=false; setTimeout(()=>{ $('uploadProgressArea').style.display='none'; $('fill').style.width='0'; $('st').innerHTML=''; }, 1500); }

    $('slideshowToggle').addEventListener('change',function(){ fetch('/slideshow?enabled='+this.checked).then(r=>r.json()).then(d=>{ if(d.status=='ok'){ refreshStatus(); showToast(this.checked?'已开启自动轮播':'已停止轮播'); } }); });
    $('saveIntervalBtn').addEventListener('click',function(){ fetch('/interval?value='+$('intervalInput').value).then(r=>r.json()).then(d=>{ if(d.status=='ok') showToast('✅ 间隔已更新'); }); });
    
    // --- 亮度防抖，优化滑动体验 ---
    var brightTimer = null;
    $('brightness').addEventListener('input',function(){ 
        var val = this.value;
        if(brightTimer) clearTimeout(brightTimer);
        brightTimer = setTimeout(function(){ fetch('/bright?v='+val); }, 50);
    });

    $('formatFSBtn').addEventListener('click',function(){ showConfirm('深度格式化：删除全部图片与笔记，不会清除 WiFi 配网。不可恢复，确定吗？', ()=>{ showToast('正在深度格式化...'); fetch('/format_fs').then(r=>r.json()).then(d=>{ if(d.status=='ok'){ loadedThumbs = [false,false,false]; refreshStatus(); refreshSystemSpace(); showToast('格式化成功'); } }); }); });
    $('resetSystemBtn').addEventListener('click',function(){ showConfirm('恢复出厂：删除图片与笔记，并清除 WiFi 配网，设备将重启。确定吗？', ()=>{ fetch('/reset_system').then(r=>r.json()).then(d=>{ if(d.status=='ok') showToast('即将重启...'); }); }); });
    
    fetch('/get_mode').then(r=>r.json()).then(d=>{
        mode=d.mode;
        $('btnImgMode').classList[mode===0?'add':'remove']('active'); $('btnClockMode').classList[mode===1?'add':'remove']('active'); $('btnNoteMode').classList[mode===2?'add':'remove']('active'); $('btnExprMode').classList[mode===3?'add':'remove']('active');
        if(mode===0) $('modeInfo').innerHTML='当前状态: 运行图片显示';
        else if(mode===1) $('modeInfo').innerHTML='当前状态: 显示实时时钟';
        else if(mode===2) $('modeInfo').innerHTML='当前状态: 显示屏幕笔记';
        else if(mode===3) $('modeInfo').innerHTML='当前状态: 随机表情动画';
    });
    
    fetch('/ota_info').then(r=>r.json()).then(d=>{ $('versionLabel').innerHTML='当前版本: V'+d.current; $('checkUpdateBtn').disabled=false; });
    
    $('checkUpdateBtn').addEventListener('click',function(){
        var btn=$('checkUpdateBtn'); btn.disabled=true; btn.innerHTML='联系服务器...';
        fetch('/ota_info').then(r=>r.json()).then(d=>{
            fetch(d.url+'?t='+Date.now()).then(r=>r.text()).then(t=>{
                var lines=t.split('\\n');
                var ver=lines[0].trim();
                var releaseNotes='';
                for(var i=1;i<lines.length;i++){ if(lines[i].trim()) releaseNotes+=lines[i].trim()+'<br>'; }
                
                var cmp = function(a, b) {
                    var pa = a.split('.'), pb = b.split('.');
                    for (var i = 0; i < Math.max(pa.length, pb.length); i++) {
                        var na = parseInt(pa[i] || 0, 10);
                        var nb = parseInt(pb[i] || 0, 10);
                        if (na > nb) return 1;
                        if (nb > na) return -1;
                    }
                    return 0;
                };

                if(cmp(ver, d.current) > 0){
                    var html=`<div style="background:rgba(46,204,113,0.1);padding:12px;border-radius:8px;margin-top:12px;border:1px solid rgba(46,204,113,0.3);">
                    <b style="color:#2ecc71;display:block;margin-bottom:8px;">✨ 发现新版本: V${ver}</b>`;
                    if(releaseNotes) html+=`<div style="font-size:12px;color:#ccc;margin-bottom:12px;line-height:1.5;text-align:left;"><b>更新内容：</b><br>${releaseNotes}</div>`;
                    html+=`<button class="btn success" id="doUpdateBtn" style="margin-top:0;">🚀 立即更新</button></div>`;
                    $('updateInfo').innerHTML=html; $('updateInfo').style.display='block';
                    $('doUpdateBtn').addEventListener('click',()=>{ showConfirm('更新期间不要断电！', ()=>{ fetch('/do_update'); showToast('OTA 指令已发送，请观察屏幕'); }); });
                    btn.style.display='none';
                } else { showToast('当前已是最新版本'); btn.innerHTML='检查更新'; btn.disabled=false; $('updateInfo').style.display='none';}
            }).catch(()=>{ showToast('连接更新服务器失败'); btn.innerHTML='检查更新'; btn.disabled=false; });
        }).catch(()=>{ showToast('获取本地信息失败'); btn.innerHTML='检查更新'; btn.disabled=false; });
    });
    
    refreshSystemSpace();
})();
</script>
</body>
</html>
)=====";

