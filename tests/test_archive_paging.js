// Run with node tests/test_archive_paging.js; no external packages required.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
class Element {
    constructor(tag='div') { this.tag=tag; this.children=[]; this.value=''; this.disabled=false; this.textContent=''; this.style={}; this.dataset={}; this.classList={toggle(){}}; }
    appendChild(child) { this.children.push(child); if(this.tag==='select' && !this.value) this.value=child.value; return child; }
    replaceChildren() { this.children=[]; this.value=''; }
    set innerHTML(value) { this.children=[]; }
    get options() { return this.children; }
    setAttribute() {}
    addEventListener() {}
    getAttribute() { return null; }
    getContext() { const owner=this; return {fillRect(){},fillText(){},beginPath(){},
        moveTo(x,y){owner.points=[[x,y]];},lineTo(x,y){owner.points.push([x,y]);},stroke(){},
        putImageData(image){owner.pixels=image.data.slice();},
        createImageData(w,h){return {data:new Uint8ClampedArray(w*h*4)};}}; }
}
const ids = new Map();
const get = id => {if(!ids.has(id))ids.set(id,new Element(id.endsWith('Select') || id==='hroFile'?'select':'div'));return ids.get(id);};
const calls=[];let fail=false, latestItem=null, latestHistoryItems=[], releaseLatest=null, deferLatest=false;
const image = name => ({png_file:'BJ'+name+'.png',json_exists:false,revision:'v1',quality:'partial',received_seconds:10,expected_seconds:1200});
const file = name => ({file:name+'.hro',name:name+'.hro',record_count:2400});
const dates = ['20261109','20261010','20260910','20260811'];
const originalFFT=Object.freeze(Array(601).fill(-10));
const header={FFT_MIN_HZ:480,FFT_CENTER_HZ:780,FFT_MAX_HZ:1080};
const context=vm.createContext({console,Map,Set,Date,Number,Math,String,Intl,BigInt,Uint8ClampedArray,setInterval:()=>0,
 document:{getElementById:get,createElement:tag=>new Element(tag),querySelectorAll:()=>[]},
 fetch:async url=>{
    calls.push(url);
    if(fail){fail=false;throw Error('simulated network error');}
    const u=new URL(url,'http://localhost');
    if(u.pathname.endsWith('/latest')) {
        if(deferLatest){deferLatest=false;await new Promise(resolve=>{releaseLatest=resolve;});}
        return {ok:true,json:async()=>({item:latestItem})};
    }
    if(u.pathname==='/api/archive/list' && latestItem) return {ok:true,json:async()=>({items:[...latestHistoryItems,latestItem]})};
    if(u.pathname.endsWith('/records')) {
        const offset=Number(u.searchParams.get('offset')||0);
        return {ok:true,json:async()=>({header,layout:'sequential',records:[{index:offset,valid:true,sequence:String(offset),timestamp_ms:1794056400000,fft_db:originalFFT}],
            record_count:2400,next_offset:offset+1200,has_more:offset===0})};
    }
    const cursor=u.searchParams.get('before')||dates[0],i=dates.indexOf(cursor);
    const hro=u.pathname.includes('/hro/');
    const items=i===0?(hro?[file('new')]:[image('202611080000'),image('202611080020')]):
                i===1?[]:i===2?(hro?[file('old')]:[image('202609010000')]):[];
    return {ok:true,json:async()=>({items,before:cursor,start_date:'start-'+i,end_date:'end-'+i,
        next_before:dates[Math.min(i+1,3)],has_more:i<2})};
 }});
const html=fs.readFileSync(path.join(__dirname,'../ui/web/archive.html'),'utf8');
vm.runInContext(html.split('<script>')[1].split('</script>')[0],context);
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 await tick();
 assert.equal(calls[0],'/api/archive/list');
 assert.equal(vm.runInContext('archiveList.length',context),2);
 vm.runInContext('showArchive(0)',context);
 await get('archiveOlder').onclick();
 assert.equal(vm.runInContext('archiveList.length',context),2); // Empty page retains data and allows further loading.
 assert.equal(get('archiveOlder').disabled,false);
 assert.equal(vm.runInContext('archiveList[currentIndex].png_file',context),'BJ202611080000.png');
 fail=true;await get('archiveOlder').onclick();
 assert.equal(get('archiveOlder').disabled,false); // Failed page keeps its cursor for retry.
 await get('archiveOlder').onclick();
 assert.equal(vm.runInContext('archiveList.length',context),3);
 assert.equal(vm.runInContext('archiveList[currentIndex].png_file',context),'BJ202611080000.png');
 assert.equal(get('archiveOlder').disabled,true);
 // Polling is idle for history, and unchanged latest data never redraws/refetches.
 const checkLatest=()=>vm.runInContext('checkLatestArchive()',context);
 let previousCalls=calls.length;
 await checkLatest();assert.equal(calls.length,previousCalls);
 vm.runInContext('showArchive(archiveList.length-1)',context);
 latestItem=image('202611080020');
 const oldSrc=get('archiveImage').src;
 latestHistoryItems=[image('202611080010')]; // Repair an existing hole even with unchanged latest.
 await checkLatest();assert.equal(get('archiveImage').src,oldSrc);
 assert.ok(vm.runInContext("archiveList.some(item=>item.png_file==='BJ202611080010.png')",context));
 const synchronizedCalls=calls.length;await checkLatest();
 assert.equal(calls.length,synchronizedCalls+1); // Unchanged latest only needs the lightweight request.
 latestItem={...latestItem,revision:'v2'};
 await checkLatest();assert.match(get('archiveImage').src,/v2/);
 latestItem=image('202611090000');
 latestHistoryItems=[image('202611080040')];
 await checkLatest();assert.match(get('archiveImage').src,/202611090000/);
 assert.match(get('archiveRange').textContent,/2026-11-09/);
 assert.equal(vm.runInContext('archiveList.length',context),6);
 assert.ok(vm.runInContext("archiveList.some(item=>item.png_file==='BJ202611080040.png')",context)); // Older loaded history retained.
 deferLatest=true;latestItem=image('202611090020');
 const waiting=checkLatest();await tick();
 vm.runInContext('showArchive(0)',context);releaseLatest();await waiting;
 assert.equal(vm.runInContext('archiveList[currentIndex].png_file',context),'BJ202609010000.png');
 previousCalls=calls.length;await checkLatest();assert.equal(calls.length,previousCalls);
 vm.runInContext('showArchive(archiveList.length-1)',context);
 fail=true;await checkLatest();await checkLatest();assert.match(get('archiveImage').src,/202611090020/);
 context.document.hidden=true;previousCalls=calls.length;await checkLatest();assert.equal(calls.length,previousCalls);context.document.hidden=false;
 console.log('Latest archive: unchanged, changed, midnight, retained history, navigation race, retry and hidden tab passed');
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../ui/web/archive_hro.js'),'utf8'),context);
 get('hroMode').onclick();await tick();await tick();
 assert.equal(get('hroFile').children.length,1);
 const networkBeforeLevel=calls.length;
 const oldPixels=get('hroCanvas').pixels.slice();
 const oldY=get('spectrumCanvas').points[0][1];
 get('hroLevelPlus').onclick();
 assert.equal(get('hroDisplayLevel').textContent,'+1 dB');
 assert.notDeepEqual(get('hroCanvas').pixels,oldPixels); // Loaded waterfall is recolored.
 assert.ok(get('spectrumCanvas').points[0][1]<oldY); // Spectrum rises with positive dB correction.
 assert.equal(calls.length,networkBeforeLevel); // No refetch or LIVE configuration writes.
 assert.equal(originalFFT[0],-10); // Frozen observation data is never modified.
 for(let n=0;n<40;++n)get('hroLevelPlus').onclick();
 assert.equal(get('hroDisplayLevel').textContent,'+30 dB');
 assert.equal(get('hroLevelPlus').disabled,true);
 get('hroLevelReset').onclick();
 assert.deepEqual(get('hroCanvas').pixels,oldPixels);
 for(let n=0;n<40;++n)get('hroLevelMinus').onclick();
 assert.equal(get('hroDisplayLevel').textContent,'-30 dB');
 assert.equal(get('hroLevelMinus').disabled,true);
 get('hroLevelReset').onclick();
 await get('hroNext').onclick();
 assert.match(get('hroStatus').textContent,/Showing 1200/);
 const recordBeforeLevel=get('recordDetails').textContent;
 get('hroLevelPlus').onclick();
 assert.equal(get('recordDetails').textContent,recordBeforeLevel); // Selected FFT survives redraw.
 const before=calls.filter(url=>url.includes('/records')).length;
 await get('hroOlder').onclick();
 assert.equal(calls.filter(url=>url.includes('/records')).length,before); // Older list must not reset selected FFT page.
 await get('hroOlder').onclick();
 assert.equal(get('hroFile').children.length,2);
 assert.equal(get('hroFile').value,'new.hro');
 assert.equal(get('hroOlder').disabled,true);
 await get('hroRefresh').onclick();
 assert.equal(get('hroFile').children.length,2); // Refresh retains all loaded periods.
 assert.match(get('hroStatus').textContent,/Showing 1200/);
 console.log('Archive display level: recoloring, spectrum, raw-data protection, limits and selection passed');
 console.log('Archive paging: 30-day requests, empty windows, retry, selection, refresh and end-of-history passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
