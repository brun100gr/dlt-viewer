#include "dltplotplugin.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

// ── Embedded HTML dashboard ──────────────────────────────────────────────────
//
// Served at GET /   — polls GET /data.json every <refreshMs> ms and renders
// two Plotly charts: signals (all traces legendonly by default) and counters
// (delta-counter and delta-timestamp sub-plots).
//
static const char INDEX_HTML[] = R"HTMLDOC(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DLT Live Plot</title>
  <style>
*{box-sizing:border-box}
body{margin:12px 16px;font-family:sans-serif;background:#f0f2f5;color:#333}
h2{margin:0 0 8px;font-size:18px}
#status{padding:7px 12px;background:#fff;border-radius:5px;margin-bottom:10px;
        font-size:12px;border:1px solid #ddd;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bar{display:flex;gap:8px;align-items:center;margin-bottom:10px;flex-wrap:wrap}
button{padding:5px 13px;cursor:pointer;border-radius:4px;border:1px solid #aaa;
       background:#eee;font-size:12px}
button:hover{background:#ddd}
label{font-size:12px}
input[type=number]{width:65px;padding:3px 5px;border:1px solid #aaa;border-radius:4px;font-size:12px}
.card{background:#fff;border-radius:6px;border:1px solid #ddd;margin-bottom:14px;padding:10px 12px}
.card-title{margin:0 0 8px;font-size:13px;color:#555;font-weight:600}
#sig-legend{display:flex;flex-wrap:wrap;gap:3px;margin-bottom:8px;
            max-height:110px;overflow-y:auto;padding-right:2px}
.li{display:inline-flex;align-items:center;gap:4px;font-size:11px;padding:2px 7px;
    border:1px solid #ddd;border-radius:3px;cursor:pointer;background:#fafafa;
    white-space:nowrap;user-select:none}
.li:hover{background:#eee}
.li input{margin:0;cursor:pointer}
.sw{display:inline-block;width:14px;height:3px;border-radius:2px;flex-shrink:0}
canvas.ch{display:block;width:100%;cursor:crosshair}
#ctr-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.cl{font-size:11px;color:#888;text-align:center;margin-bottom:2px}
  </style>
</head>
<body>
  <h2>DLT Live Plot</h2>
  <div id="status">Connecting&#8230;</div>
  <div class="bar">
    <button id="btnPause">&#9646;&#9646;&nbsp;Pause</button>
    <button id="btnReset">Reset&nbsp;Zoom</button>
    <label>Refresh every
      <input type="number" id="refreshMs" value="2000" min="200" max="60000"> ms
    </label>
  </div>

  <div class="card">
    <p class="card-title">Signals &#8212; tick checkboxes in legend to show/hide traces</p>
    <div id="sig-legend"></div>
    <canvas class="ch" id="sig-canvas" style="height:420px"></canvas>
  </div>

  <div class="card" id="ctr-card" style="display:none">
    <p class="card-title">Counters</p>
    <div id="ctr-grid"></div>
  </div>

  <script>
(function () {
'use strict';

/* ── roundRect polyfill (Chrome < 99 / old Firefox) ─── */
if (!CanvasRenderingContext2D.prototype.roundRect) {
  CanvasRenderingContext2D.prototype.roundRect = function (x,y,w,h,r) {
    this.beginPath();
    this.moveTo(x+r, y);
    this.arcTo(x+w, y,   x+w, y+h, r);
    this.arcTo(x+w, y+h, x,   y+h, r);
    this.arcTo(x,   y+h, x,   y,   r);
    this.arcTo(x,   y,   x+w, y,   r);
    this.closePath();
  };
}

/* ── colour palette ─────────────────────────────────── */
var PAL=['#1f77b4','#ff7f0e','#2ca02c','#d62728','#9467bd',
         '#8c564b','#e377c2','#7f7f7f','#bcbd22','#17becf',
         '#aec7e8','#ffbb78','#98df8a','#ff9896','#c5b0d5',
         '#c49c94','#f7b6d2','#c7c7c7','#dbdb8d','#9edae5'];

/* ── chart margins ──────────────────────────────────── */
var PAD={t:24,r:18,b:44,l:72};

/* ── number helpers ─────────────────────────────────── */
function niceStep(range,n){
  var s=range/n, m=Math.pow(10,Math.floor(Math.log10(s))), f=s/m;
  return(f<1.5?1:f<3?2:f<7?5:10)*m;
}
function axisTicks(lo,hi,n){
  if(lo===hi)return[lo];
  var s=niceStep(hi-lo,n), t0=Math.ceil(lo/s)*s, ts=[];
  for(var v=t0; v<=hi+s*0.001; v+=s) ts.push(parseFloat(v.toPrecision(10)));
  return ts;
}
function fmt(v){
  if(v===0)return'0';
  var a=Math.abs(v);
  if(a>=1e4||a<1e-3)return v.toExponential(2);
  return parseFloat(v.toPrecision(5)).toString();
}

/* ── core canvas chart ──────────────────────────────── */
/* traces: [{name,x:[],y:[],color,visible}]
   returns extent object, or null                        */
function drawChart(canvas, traces, xlo, xhi) {
  var dpr=window.devicePixelRatio||1;
  var cW=canvas.offsetWidth||600;
  var cH=canvas.offsetHeight||280;
  canvas.width =cW*dpr;
  canvas.height=cH*dpr;
  var ctx=canvas.getContext('2d');
  ctx.scale(dpr,dpr);
  var pw=cW-PAD.l-PAD.r, ph=cH-PAD.t-PAD.b;

  ctx.fillStyle='#fff'; ctx.fillRect(0,0,cW,cH);

  /* data extent */
  var xd0=Infinity,xd1=-Infinity,yd0=Infinity,yd1=-Infinity;
  traces.forEach(function(t){
    if(!t.visible||!t.x.length)return;
    t.x.forEach(function(v){if(v<xd0)xd0=v;if(v>xd1)xd1=v;});
    t.y.forEach(function(v){if(v<yd0)yd0=v;if(v>yd1)yd1=v;});
  });
  var has=xd0<Infinity;
  if(xlo==null)xlo=has?xd0:0;
  if(xhi==null)xhi=has?xd1:1;
  if(xlo===xhi){xlo-=0.5;xhi+=0.5;}
  if(!has){yd0=0;yd1=1;}
  if(yd0===yd1){yd0-=0.5;yd1+=0.5;}
  var yp=(yd1-yd0)*0.07; yd0-=yp; yd1+=yp;
  var xRng=xhi-xlo, yRng=yd1-yd0;

  function tx(v){return PAD.l+(v-xlo)/xRng*pw;}
  function ty(v){return PAD.t+ph-(v-yd0)/yRng*ph;}

  /* grid */
  var xt=axisTicks(xlo,xhi,6), yt=axisTicks(yd0,yd1,5);
  ctx.strokeStyle='#ececec'; ctx.lineWidth=1;
  xt.forEach(function(v){var x=tx(v);if(x<PAD.l-1||x>PAD.l+pw+1)return;
    ctx.beginPath();ctx.moveTo(x,PAD.t);ctx.lineTo(x,PAD.t+ph);ctx.stroke();});
  yt.forEach(function(v){var y=ty(v);if(y<PAD.t-1||y>PAD.t+ph+1)return;
    ctx.beginPath();ctx.moveTo(PAD.l,y);ctx.lineTo(PAD.l+pw,y);ctx.stroke();});

  /* border */
  ctx.strokeStyle='#bbb'; ctx.lineWidth=1;
  ctx.strokeRect(PAD.l,PAD.t,pw,ph);

  /* x axis labels */
  ctx.fillStyle='#666'; ctx.font='11px sans-serif'; ctx.textAlign='center';
  xt.forEach(function(v){var x=tx(v);if(x<PAD.l-1||x>PAD.l+pw+1)return;
    ctx.fillText(v.toFixed(3),x,PAD.t+ph+14);});
  ctx.fillText('time (s)',PAD.l+pw/2,PAD.t+ph+32);

  /* y axis labels */
  ctx.textAlign='right';
  yt.forEach(function(v){var y=ty(v);if(y<PAD.t-1||y>PAD.t+ph+1)return;
    ctx.fillText(fmt(v),PAD.l-5,y+4);});

  /* no-data hint */
  if(!has){
    ctx.fillStyle='#aaa'; ctx.font='13px sans-serif'; ctx.textAlign='center';
    ctx.fillText('No data \u2014 tick a trace in the legend above',PAD.l+pw/2,PAD.t+ph/2);
  }

  /* clip to plot area */
  ctx.save();
  ctx.beginPath(); ctx.rect(PAD.l,PAD.t,pw,ph); ctx.clip();

  /* HV step-line traces */
  traces.forEach(function(t){
    if(!t.visible||!t.x.length)return;
    ctx.strokeStyle=t.color; ctx.lineWidth=1.5;
    ctx.beginPath();
    var pcy=0;
    for(var i=0;i<t.x.length;i++){
      var cx=tx(t.x[i]),cy=ty(t.y[i]);
      if(i===0){ctx.moveTo(cx,cy);}
      else{ctx.lineTo(cx,pcy);ctx.lineTo(cx,cy);}
      pcy=cy;
    }
    ctx.stroke();
    /* markers */
    ctx.fillStyle=t.color;
    for(var j=0;j<t.x.length;j++){
      var cx2=tx(t.x[j]);
      if(cx2<PAD.l-5||cx2>PAD.l+pw+5)continue;
      ctx.beginPath(); ctx.arc(cx2,ty(t.y[j]),2.5,0,6.2832); ctx.fill();
    }
  });
  ctx.restore();
  return{tx:tx,ty:ty,xlo:xlo,xhi:xhi,xRng:xRng,pw:pw,ph:ph,cH:cH};
}

/* ── hover overlay ──────────────────────────────────── */
function drawHover(canvas,ext,mouse,traces){
  if(!mouse||!ext)return;
  var mx=mouse.x,my=mouse.y;
  if(mx<PAD.l||mx>PAD.l+ext.pw||my<PAD.t||my>PAD.t+ext.ph)return;
  var dpr=window.devicePixelRatio||1;
  var cW=canvas.width/dpr;
  var ctx=canvas.getContext('2d');

  /* crosshair */
  ctx.save();
  ctx.beginPath(); ctx.rect(PAD.l,PAD.t,ext.pw,ext.ph); ctx.clip();
  ctx.strokeStyle='rgba(0,0,0,0.18)'; ctx.lineWidth=1;
  ctx.setLineDash([4,4]);
  ctx.beginPath(); ctx.moveTo(mx,PAD.t); ctx.lineTo(mx,PAD.t+ext.ph); ctx.stroke();
  ctx.setLineDash([]);
  ctx.restore();

  /* tooltip */
  var hx=ext.xlo+(mx-PAD.l)/ext.pw*ext.xRng;
  var lines=['t = '+hx.toFixed(4)+' s'], cols=['#555'];
  traces.forEach(function(t){
    if(!t.visible||!t.x.length)return;
    var b=0,bd=Math.abs(t.x[0]-hx);
    for(var i=1;i<t.x.length;i++){var d=Math.abs(t.x[i]-hx);if(d<bd){bd=d;b=i;}}
    lines.push(t.name+': '+fmt(t.y[b])); cols.push(t.color);
  });
  if(lines.length===1)return;

  ctx.save();
  ctx.font='11px monospace';
  var lh=16,bp=7,bw=0;
  lines.forEach(function(l){bw=Math.max(bw,ctx.measureText(l).width+bp*2+4);});
  var bh=lines.length*lh+bp+2;
  var bx=mx+14, by=Math.max(PAD.t+2,Math.min(ext.cH-bh-4,my-bh/2));
  if(bx+bw>cW-4)bx=mx-bw-14;
  ctx.fillStyle='rgba(255,255,255,0.94)'; ctx.strokeStyle='#ccc'; ctx.lineWidth=1;
  ctx.beginPath(); ctx.roundRect(bx,by,bw,bh,4); ctx.fill(); ctx.stroke();
  lines.forEach(function(l,i){ctx.fillStyle=cols[i]; ctx.fillText(l,bx+bp,by+(i+1)*lh);});
  ctx.restore();
}

/* ── signals chart state ────────────────────────────── */
var sigCanvas=document.getElementById('sig-canvas');
var sigLegend=document.getElementById('sig-legend');
var sigTraces=[], sigExt=null;
var sigView={xMin:null,xMax:null};
var sigMouse=null;
var sigDrag={on:false,sx:0,vMin:0,vMax:0};

function rebuildLegend(rawTraces){
  /* preserve per-trace visibility */
  var vis={};
  sigTraces.forEach(function(t){vis[t.name]=t.visible;});

  var oldN=sigTraces.map(function(t){return t.name;});
  var newN=rawTraces.map(function(t){return t.name;});
  var namesChanged=(JSON.stringify(oldN)!==JSON.stringify(newN));

  if(namesChanged){
    /* full rebuild: new array, new DOM */
    sigTraces=rawTraces.map(function(t,i){
      return{name:t.name,x:t.x,y:t.y,color:PAL[i%PAL.length],
             visible:t.name in vis?vis[t.name]:false};
    });
    sigLegend.innerHTML='';
    sigTraces.forEach(function(t){
      var lbl=document.createElement('label'); lbl.className='li'; lbl.dataset.n=t.name;
      var cb=document.createElement('input'); cb.type='checkbox'; cb.checked=t.visible;
      /* look up by name at click time so we always hit the live sigTraces entry */
      cb.addEventListener('change',function(){
        var tr=sigTraces.find(function(s){return s.name===lbl.dataset.n;});
        if(tr){tr.visible=cb.checked;}
        redrawSig();
      });
      var sw=document.createElement('span'); sw.className='sw'; sw.style.background=t.color;
      lbl.appendChild(cb); lbl.appendChild(sw);
      lbl.appendChild(document.createTextNode(t.name));
      sigLegend.appendChild(lbl);
    });
  } else {
    /* names unchanged: update data IN-PLACE so closures remain valid */
    rawTraces.forEach(function(rt,i){
      sigTraces[i].x=rt.x;
      sigTraces[i].y=rt.y;
    });
    /* sync checkbox state (do NOT overwrite user's visibility choice) */
    sigLegend.querySelectorAll('.li').forEach(function(el){
      var t=sigTraces.find(function(t){return t.name===el.dataset.n;});
      if(t)el.querySelector('input').checked=t.visible;
    });
  }
}

function redrawSig(){
  sigExt=drawChart(sigCanvas,sigTraces,sigView.xMin,sigView.xMax);
  drawHover(sigCanvas,sigExt,sigMouse,sigTraces);
}

sigCanvas.addEventListener('mousemove',function(e){
  var r=sigCanvas.getBoundingClientRect();
  sigMouse={x:e.clientX-r.left,y:e.clientY-r.top};
  if(sigDrag.on&&sigExt){
    var dx=sigMouse.x-sigDrag.sx;
    var dv=-dx/sigExt.pw*sigExt.xRng;
    sigView.xMin=sigDrag.vMin+dv; sigView.xMax=sigDrag.vMax+dv;
  }
  redrawSig();
});
sigCanvas.addEventListener('mousedown',function(e){
  var r=sigCanvas.getBoundingClientRect();
  sigDrag.on=true; sigDrag.sx=e.clientX-r.left;
  sigDrag.vMin=sigView.xMin!==null?sigView.xMin:(sigExt?sigExt.xlo:0);
  sigDrag.vMax=sigView.xMax!==null?sigView.xMax:(sigExt?sigExt.xhi:1);
});
sigCanvas.addEventListener('mouseup',   function(){sigDrag.on=false;});
sigCanvas.addEventListener('mouseleave',function(){sigDrag.on=false;sigMouse=null;redrawSig();});
sigCanvas.addEventListener('wheel',function(e){
  e.preventDefault();
  if(!sigExt)return;
  var r=sigCanvas.getBoundingClientRect();
  var xa=sigExt.xlo+(e.clientX-r.left-PAD.l)/sigExt.pw*sigExt.xRng;
  var f=e.deltaY>0?1.25:0.8;
  sigView.xMin=xa-(xa-sigExt.xlo)*f; sigView.xMax=xa+(sigExt.xhi-xa)*f;
  redrawSig();
},{passive:false});
sigCanvas.addEventListener('dblclick',function(){
  sigView.xMin=null; sigView.xMax=null; redrawSig();
});
document.getElementById('btnReset').addEventListener('click',function(){
  sigView.xMin=null; sigView.xMax=null; redrawSig();
});
new ResizeObserver(function(){redrawSig();}).observe(sigCanvas);

/* ── counter charts ─────────────────────────────────── */
var ctrGrid=document.getElementById('ctr-grid');
var ctrCard=document.getElementById('ctr-card');
var ctrCvs={};

function updateCounters(ctr){
  var comps=Object.keys(ctr).filter(function(k){
    return ctr[k]&&ctr[k].length>=2;
  }).sort();
  if(!comps.length){ctrCard.style.display='none';return;}
  ctrCard.style.display='';
  comps.forEach(function(comp){
    if(!ctrCvs[comp]){
      ctrCvs[comp]={};
      ['dv','dt'].forEach(function(kind){
        var d=document.createElement('div');
        var lbl=document.createElement('div'); lbl.className='cl';
        lbl.textContent=comp+(kind==='dv'?' \u2014 \u0394Counter':' \u2014 \u0394Ts (ms)');
        var cv=document.createElement('canvas'); cv.className='ch'; cv.style.height='200px';
        d.appendChild(lbl); d.appendChild(cv);
        ctrGrid.appendChild(d);
        ctrCvs[comp][kind]=cv;
      });
    }
    var pts=ctr[comp];
    var xs=pts.map(function(p){return p[0];}), ys=pts.map(function(p){return p[1];});
    var xs1=xs.slice(0,-1);
    var dvY=ys.slice(1).map(function(v,i){return v-ys[i];});
    var dtY=xs.slice(1).map(function(t,i){return(t-xs[i])*1000;});
    drawChart(ctrCvs[comp].dv,[{name:comp,x:xs1,y:dvY,color:'#1f77b4',visible:true}],null,null);
    drawChart(ctrCvs[comp].dt,[{name:comp,x:xs1,y:dtY,color:'#ff7f0e',visible:true}],null,null);
  });
}

/* ── pause ──────────────────────────────────────────── */
var paused=false;
document.getElementById('btnPause').addEventListener('click',function(){
  paused=!paused;
  this.innerHTML=paused?'&#9654;&nbsp;Resume':'&#9646;&#9646;&nbsp;Pause';
});

/* ── fetch & update ─────────────────────────────────── */
function fetchAndUpdate(){
  if(!paused){
    fetch('/data.json')
      .then(function(r){return r.ok?r.json():Promise.reject(r.status);})
      .then(function(data){
        var sig=data.signals||{}, ctr=data.counters||{};
        var raw=[];
        Object.keys(sig).sort().forEach(function(comp){
          ['inp','out'].forEach(function(dir){
            Object.keys(sig[comp][dir]||{}).sort().forEach(function(sn){
              var pts=sig[comp][dir][sn];
              if(!pts||!pts.length)return;
              raw.push({name:comp+(dir==='inp'?'_Inp_':'_Out_')+sn,
                        x:pts.map(function(p){return p[0];}),
                        y:pts.map(function(p){return p[1];})});
            });
          });
        });
        rebuildLegend(raw);
        redrawSig();
        updateCounters(ctr);
        var s=data.stats||{};
        document.getElementById('status').textContent=
          'Last update: '+new Date().toLocaleTimeString()+
          '  |  Received: '+(s.received||0)+
          '  |  Matched: '+(s.matched||0)+
          '  |  Signal components: '+(s.signal_components||0)+
          '  |  Counter components: '+(s.counter_components||0);
      })
      .catch(function(e){
        document.getElementById('status').textContent='Fetch error: '+e;
      });
  }
  setTimeout(fetchAndUpdate,parseInt(document.getElementById('refreshMs').value,10)||2000);
}

fetchAndUpdate();
}());
  </script>
</body>
</html>
)HTMLDOC";
// ─────────────────────────────────────────────────────────────────────────────

// ── helpers ──────────────────────────────────────────────────────────────────

static QString normalizeDirection(const QString &direction)
{
    const QString d = direction.toLower();
    if (d == QLatin1String("received value") ||
        d == QLatin1String("in")             ||
        d == QLatin1String("inp"))
        return QStringLiteral("inp");
    if (d == QLatin1String("send value") ||
        d == QLatin1String("out"))
        return QStringLiteral("out");
    return QString();
}

// ── constructor / destructor ─────────────────────────────────────────────────

DltPlotPlugin::DltPlotPlugin()
{
    // ── Signal patterns  (Qt named captures: (?<name>...) )
    // Equivalent to the dlt_plot.py "signals" patterns list.
    signalPatterns = {
        {   // ADAS v130 : [ts][COMP ADF_][level] COMP_IN_SIGNAL : value
            QRegularExpression(QStringLiteral(
                R"(\[(?<ts>\d+)\]\[(?<comp_unused>\w+)\s+ADF_\]\[\w+\]\s)"
                R"((?<comp>\w+)_(?<direction>IN|OUT)_(?<signal>\w+)\s?:\s)"
                R"((?<value>-?\d+(\.\d+)?))") ),
            QStringLiteral("adas_v130")
        },
        {   // ADAS v120 : [ts][COMP FCT_][level] COMP dir SIGNAL: value
            QRegularExpression(QStringLiteral(
                R"(\[(?<ts>\d+)\]\[(?<comp_unused>\w+)\s+FCT_\]\[\w+\]\s)"
                R"((?<comp>\w+)\s(?<direction>\w+)\s(?<signal>\w+):\s)"
                R"((?<value>-?\d+(\.\d+)?))") ),
            QStringLiteral("adas_v120")
        },
        {   // HMI v130 : [ts][COMP CTX][level] 'SIGNAL (received|send) Value'value
            QRegularExpression(QStringLiteral(
                R"(\[(?<ts>\d+)\]\[(?<comp>\w+)\s+\w+\]\[\w+\]\s+')"
                R"((?<signal>\w+)(\sinit)?\s(?<direction>(received|send)\sValue)')"
                R"((?<value>-?\d+(\.\d+)?))") ),
            QStringLiteral("hmi_v130")
        }
    };

    // ── Counter patterns
    counterPatterns = {
        {   // ADAS v130
            QRegularExpression(QStringLiteral(
                R"(\[(?<ts>\d+)\]\[(?<comp>\w+)\s+ADF_\]\[\w+\]\s)"
                R"(\w+\scounter(\svalue)?\s?:\s(?<value>\d+))") ),
            QStringLiteral("counter_adas_v130")
        },
        {   // ADAS v120
            QRegularExpression(QStringLiteral(
                R"(\[(?<ts>\d+)\]\[(?<comp_unused>\w+)\s+FCT_\]\[\w+\]\s)"
                R"((?<comp>\w+)\scounter(\svalue)?:\s(?<value>\d+))") ),
            QStringLiteral("counter_adas_v120")
        },
        {   // HMI
            QRegularExpression(QStringLiteral(
                R"(\[(?<ts>\d+)\]\[(?<comp_unused>\w+)\s+\w+\]\[\w+\]\s)"
                R"(\[Counter\svalue\sfor\s(?<comp>\w+)\]:\s(?<value>\d+))") ),
            QStringLiteral("counter_hmi")
        }
    };
}

DltPlotPlugin::~DltPlotPlugin()
{
    if (tcpServer)
        tcpServer->close();
}

// ── QDLTPluginInterface ───────────────────────────────────────────────────────

QString DltPlotPlugin::name()                   { return QStringLiteral("DLT Plot Plugin"); }
QString DltPlotPlugin::pluginVersion()          { return QStringLiteral(DLT_PLOT_PLUGIN_VERSION); }
QString DltPlotPlugin::pluginInterfaceVersion() { return QStringLiteral(PLUGIN_INTERFACE_VERSION); }
QString DltPlotPlugin::description()            { return QStringLiteral("Real-time Plotly signal/counter dashboard via embedded HTTP server"); }
QString DltPlotPlugin::error()                  { return errorText; }
bool    DltPlotPlugin::loadConfig(QString)      { return true; }
bool    DltPlotPlugin::saveConfig(QString)      { return true; }
QStringList DltPlotPlugin::infoConfig()         { return {}; }

// ── QDltPluginViewerInterface ────────────────────────────────────────────────

QWidget* DltPlotPlugin::initViewer()
{
    widget = new QWidget();
    auto *vl = new QVBoxLayout(widget);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(6);

    // URL row — clickable link
    labelUrl = new QLabel(QStringLiteral("Server not started"));
    labelUrl->setTextFormat(Qt::RichText);
    labelUrl->setOpenExternalLinks(true);
    vl->addWidget(labelUrl);

    // Buttons row
    auto *hl = new QHBoxLayout;
    btnBrowser = new QPushButton(QStringLiteral("Open in Browser"));
    btnClear   = new QPushButton(QStringLiteral("Clear Data"));
    hl->addWidget(btnBrowser);
    hl->addWidget(btnClear);
    hl->addStretch();
    vl->addLayout(hl);

    // Status row
    labelStatus = new QLabel(
        QStringLiteral("Received: 0 | Matched: 0 | Signal components: 0 | Counter components: 0"));
    labelStatus->setWordWrap(true);
    vl->addWidget(labelStatus);
    vl->addStretch();

    connect(btnBrowser, &QPushButton::clicked, this, &DltPlotPlugin::onOpenBrowser);
    connect(btnClear,   &QPushButton::clicked, this, &DltPlotPlugin::onClearData);

    // Start HTTP server and update label
    if (startServer()) {
        const QString url = QStringLiteral("http://localhost:%1").arg(serverPort);
        labelUrl->setText(QStringLiteral("<a href='%1'>%1</a>").arg(url));
    } else {
        labelUrl->setText(
            QStringLiteral("<font color='red'>Failed to start server: %1</font>").arg(errorText));
        btnBrowser->setEnabled(false);
    }

    // Periodic UI refresh (main thread only — no mutex needed for the label itself)
    auto *uiTimer = new QTimer(widget);
    connect(uiTimer, &QTimer::timeout, this, &DltPlotPlugin::updateStatusLabel);
    uiTimer->start(2000);

    return widget;
}

// called when a new file is loaded — reset all data
void DltPlotPlugin::initFileStart(QDltFile *)
{
    QMutexLocker lk(&dataMutex);
    dataSignals.clear();
    dataCounters.clear();
    receivedCount = 0;
    matchedCount  = 0;
}

void DltPlotPlugin::initFileFinish()                         {}
void DltPlotPlugin::initMsg(int, QDltMsg &)                  {}
void DltPlotPlugin::updateFileStart()                        {}
void DltPlotPlugin::updateMsg(int, QDltMsg &)                {}
void DltPlotPlugin::updateFileFinish()                       {}
void DltPlotPlugin::selectedIdxMsg(int, QDltMsg &)           {}
void DltPlotPlugin::selectedIdxMsgDecoded(int, QDltMsg &)    {}

// Both called by the viewer — initMsgDecoded for file load, updateMsgDecoded for live
void DltPlotPlugin::initMsgDecoded(int, QDltMsg &msg)   { processMsg(msg); }
void DltPlotPlugin::updateMsgDecoded(int, QDltMsg &msg) { processMsg(msg); }

// ── message processing ───────────────────────────────────────────────────────

void DltPlotPlugin::processMsg(QDltMsg &msg)
{
    // Build a line in the same format as dlt_plot.py:
    // "[<ts_µs>][<apid> <ctid>][LOG] <payload>"
    // The log-level field just needs to be a single \w+ token (not captured by patterns).
    const qint64  ts_us = static_cast<qint64>(msg.getTimestamp()) * 100LL;
    const QString line  = QStringLiteral("[%1][%2 %3][LOG] %4")
                              .arg(ts_us)
                              .arg(msg.getApid(), msg.getCtid(), msg.toStringPayload());
    parseLine(line);
}

void DltPlotPlugin::parseLine(const QString &line)
{
    // ── match signal patterns ─────────────────────────────────────────
    bool    sigMatched = false;
    double  sigTs = 0.0, sigVal = 0.0;
    QString sigComp, sigDir, sigName;

    for (const Pattern &pat : qAsConst(signalPatterns)) {
        const QRegularExpressionMatch m = pat.re.match(line);
        if (!m.hasMatch()) continue;

        sigTs   = m.captured(QStringLiteral("ts")).toDouble() / 1.0e6;
        sigComp = m.captured(QStringLiteral("comp"));
        sigDir  = normalizeDirection(m.captured(QStringLiteral("direction")));
        sigName = m.captured(QStringLiteral("signal"));
        bool ok;
        sigVal = m.captured(QStringLiteral("value")).toDouble(&ok);

        if (ok && !sigDir.isEmpty() && !sigComp.isEmpty() && !sigName.isEmpty())
            sigMatched = true;
        break;  // first matching pattern wins, as in the Python script
    }

    // ── match counter patterns ────────────────────────────────────────
    bool    ctrMatched = false;
    double  ctrTs = 0.0, ctrVal = 0.0;
    QString ctrComp;

    for (const Pattern &pat : qAsConst(counterPatterns)) {
        const QRegularExpressionMatch m = pat.re.match(line);
        if (!m.hasMatch()) continue;

        ctrTs   = m.captured(QStringLiteral("ts")).toDouble() / 1.0e6;
        ctrComp = m.captured(QStringLiteral("comp"));
        bool ok;
        ctrVal = m.captured(QStringLiteral("value")).toDouble(&ok);

        if (ok && !ctrComp.isEmpty())
            ctrMatched = true;
        break;
    }

    // ── update shared data under a single lock ────────────────────────
    QMutexLocker lk(&dataMutex);
    ++receivedCount;
    if (sigMatched || ctrMatched) ++matchedCount;

    if (sigMatched) {
        SignalData &cd = dataSignals[sigComp];
        if (sigDir == QLatin1String("inp"))
            cd.inp[sigName].append(qMakePair(sigTs, sigVal));
        else
            cd.out[sigName].append(qMakePair(sigTs, sigVal));
    }
    if (ctrMatched) {
        dataCounters[ctrComp].append(qMakePair(ctrTs, ctrVal));
    }
}

// ── HTTP server ───────────────────────────────────────────────────────────────

bool DltPlotPlugin::startServer()
{
    tcpServer = new QTcpServer(this);
    connect(tcpServer, &QTcpServer::newConnection,
            this,      &DltPlotPlugin::onNewConnection);

    for (int port = 8088; port <= 8099; ++port) {
        if (tcpServer->listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
            serverPort = port;
            return true;
        }
    }
    errorText = tcpServer->errorString();
    return false;
}

void DltPlotPlugin::onNewConnection()
{
    while (tcpServer->hasPendingConnections()) {
        QTcpSocket *socket = tcpServer->nextPendingConnection();
        socketBuffers[socket] = QByteArray{};

        // Accumulate data until we have a complete HTTP request header
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            if (!socketBuffers.contains(socket)) return;
            socketBuffers[socket] += socket->readAll();
            const QByteArray &buf = socketBuffers[socket];
            if (buf.contains("\r\n\r\n") || buf.contains("\n\n")) {
                handleHttpRequest(socket, buf);
                socketBuffers.remove(socket);
                socket->disconnectFromHost();
            }
        });

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            socketBuffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void DltPlotPlugin::handleHttpRequest(QTcpSocket *socket, const QByteArray &request)
{
    // Parse first line: "GET /path HTTP/1.1"
    const int       nl        = request.indexOf('\n');
    if (nl < 0) return;
    const QByteArray firstLine = request.left(nl).trimmed();
    const QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 2) return;

    const QByteArray path = parts.at(1);

    if (path == "/" || path == "/index.html") {
        sendHttpResponse(socket, 200,
                         QByteArrayLiteral("text/html; charset=utf-8"),
                         QByteArray(INDEX_HTML));
    } else if (path == "/data.json") {
        sendHttpResponse(socket, 200,
                         QByteArrayLiteral("application/json"),
                         buildDataJson());
    } else {
        sendHttpResponse(socket, 404,
                         QByteArrayLiteral("text/plain"),
                         QByteArrayLiteral("Not Found"));
    }
}

void DltPlotPlugin::sendHttpResponse(QTcpSocket *socket, int code,
                                     const QByteArray &contentType,
                                     const QByteArray &body)
{
    QByteArray hdr;
    hdr.reserve(256);
    hdr += "HTTP/1.1 ";
    hdr += QByteArray::number(code);
    hdr += (code == 200) ? " OK\r\n" : " Error\r\n";
    hdr += "Content-Type: ";
    hdr += contentType;
    hdr += "\r\nContent-Length: ";
    hdr += QByteArray::number(body.size());
    hdr += "\r\nCache-Control: no-cache\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Connection: close\r\n"
           "\r\n";
    socket->write(hdr);
    socket->write(body);
    socket->flush();
}

// ── JSON serialisation ────────────────────────────────────────────────────────

QByteArray DltPlotPlugin::buildDataJson()
{
    QMutexLocker lk(&dataMutex);

    // ── signals
    QJsonObject sigObj;
    for (auto ci = dataSignals.constBegin(); ci != dataSignals.constEnd(); ++ci) {
        QJsonObject inpObj, outObj;

        for (auto si = ci->inp.constBegin(); si != ci->inp.constEnd(); ++si) {
            QJsonArray pts;
            for (const auto &p : si.value()) {
                pts.append(QJsonArray{p.first, p.second});
            }
            inpObj.insert(si.key(), pts);
        }
        for (auto si = ci->out.constBegin(); si != ci->out.constEnd(); ++si) {
            QJsonArray pts;
            for (const auto &p : si.value()) {
                pts.append(QJsonArray{p.first, p.second});
            }
            outObj.insert(si.key(), pts);
        }

        QJsonObject compObj;
        compObj.insert(QStringLiteral("inp"), inpObj);
        compObj.insert(QStringLiteral("out"), outObj);
        sigObj.insert(ci.key(), compObj);
    }

    // ── counters
    QJsonObject ctrObj;
    for (auto ci = dataCounters.constBegin(); ci != dataCounters.constEnd(); ++ci) {
        QJsonArray pts;
        for (const auto &p : ci.value()) {
            pts.append(QJsonArray{p.first, p.second});
        }
        ctrObj.insert(ci.key(), pts);
    }

    // ── stats
    QJsonObject statsObj;
    statsObj.insert(QStringLiteral("received"),          receivedCount);
    statsObj.insert(QStringLiteral("matched"),           matchedCount);
    statsObj.insert(QStringLiteral("signal_components"), static_cast<int>(dataSignals.size()));
    statsObj.insert(QStringLiteral("counter_components"),static_cast<int>(dataCounters.size()));

    QJsonObject root;
    root.insert(QStringLiteral("signals"),  sigObj);
    root.insert(QStringLiteral("counters"), ctrObj);
    root.insert(QStringLiteral("stats"),    statsObj);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// ── slots ─────────────────────────────────────────────────────────────────────

void DltPlotPlugin::onOpenBrowser()
{
    if (serverPort > 0)
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("http://localhost:%1").arg(serverPort)));
}

void DltPlotPlugin::onClearData()
{
    QMutexLocker lk(&dataMutex);
    dataSignals.clear();
    dataCounters.clear();
    receivedCount = 0;
    matchedCount  = 0;
}

void DltPlotPlugin::updateStatusLabel()
{
    if (!labelStatus) return;
    QMutexLocker lk(&dataMutex);
    labelStatus->setText(
        QStringLiteral("Received: %1 | Matched: %2 | Signal components: %3 | Counter components: %4")
            .arg(receivedCount).arg(matchedCount)
            .arg(dataSignals.size()).arg(dataCounters.size()));
}
