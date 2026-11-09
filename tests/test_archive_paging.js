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
    getContext() { return {fillRect(){},fillText(){},beginPath(){},moveTo(){},lineTo(){},stroke(){},putImageData(){},
        createImageData(w,h){return {data:new Uint8ClampedArray(w*h*4)};}}; }
}
const ids = new Map();
const get = id => {if(!ids.has(id))ids.set(id,new Element(id.endsWith('Select') || id==='hroFile'?'select':'div'));return ids.get(id);};
const calls=[];let fail=false;
const image = name => ({png_file:'BJ'+name+'.png',json_exists:false,quality:'partial',received_seconds:10,expected_seconds:1200});
const file = name => ({file:name+'.hro',name:name+'.hro',record_count:2400});
const dates = ['20261109','20261010','20260910','20260811'];
const header={FFT_MIN_HZ:480,FFT_CENTER_HZ:780,FFT_MAX_HZ:1080};
const context=vm.createContext({console,Map,Set,Date,Number,Math,String,Intl,BigInt,Uint8ClampedArray,
 document:{getElementById:get,createElement:tag=>new Element(tag),querySelectorAll:()=>[]},
 fetch:async url=>{
    calls.push(url);
    if(fail){fail=false;throw Error('simulated network error');}
    const u=new URL(url,'http://localhost');
    if(u.pathname.endsWith('/records')) {
        const offset=Number(u.searchParams.get('offset')||0);
        return {ok:true,json:async()=>({header,layout:'sequential',records:[{index:offset,valid:false,sequence:String(offset),timestamp_ms:1794056400000}],
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
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../ui/web/archive_hro.js'),'utf8'),context);
 get('hroMode').onclick();await tick();await tick();
 assert.equal(get('hroFile').children.length,1);
 await get('hroNext').onclick();
 assert.match(get('hroStatus').textContent,/Showing 1200/);
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
 console.log('Archive paging: 30-day requests, empty windows, retry, selection, refresh and end-of-history passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
