/* Host integration test: real embedded assets/UI, synthetic SD transaction server. */
const fs=require('fs'),path=require('path'),http=require('http'),zlib=require('zlib'),crypto=require('crypto'),assert=require('assert');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..'),out=path.join(root,'validation-fixtures/pdf-test');fs.mkdirSync(out,{recursive:true});
const html=fs.readFileSync(path.join(root,'ReaderWebAssets.h'),'utf8').split('R"INKWEB(')[1].split(')INKWEB"')[0];
const code=fs.readFileSync(path.join(root,'ReaderPdfAssets.cpp'),'utf8'),manifest=JSON.parse(fs.readFileSync(path.join(root,'pdf-assets-manifest.json'),'utf8'));
const assets=new Map();for(const match of code.matchAll(/static const uint8_t asset(\d+)\[\] PROGMEM=\{([0-9,]+)\};/g)){const entry=manifest.assets[Number(match[1])],data=Buffer.from(JSON.parse('['+match[2]+']'));assert.equal(crypto.createHash('sha256').update(zlib.gunzipSync(data)).digest('hex'),entry.sha256);assets.set(entry.path,{...entry,data});}
assert.equal(assets.size,manifest.assets.length);
let files=new Map(),active=null,retryInjected=false,cancels=0,heartbeats=0,requests=[],cached=new Map();
const crcTable=Array.from({length:256},(_,n)=>{for(let k=0;k<8;k++)n=(n>>>1)^((n&1)?0xedb88320:0);return n>>>0;});
const crc=b=>{let c=0xffffffff;for(const v of b)c=(c>>>8)^crcTable[(c^v)&255];return (~c)>>>0;};
function validateBook(data,expectedPages){
 const end=data.length-22;assert.equal(data.readUInt32LE(end),0x06054b50);const entries=new Map();let cursor=data.readUInt32LE(end+16),count=data.readUInt16LE(end+10);
 for(let i=0;i<count;i++){assert.equal(data.readUInt32LE(cursor),0x02014b50);const method=data.readUInt16LE(cursor+10),sum=data.readUInt32LE(cursor+16),size=data.readUInt32LE(cursor+20),nameLength=data.readUInt16LE(cursor+28),extra=data.readUInt16LE(cursor+30),comment=data.readUInt16LE(cursor+32),offset=data.readUInt32LE(cursor+42),name=data.subarray(cursor+46,cursor+46+nameLength).toString();assert.equal(data.readUInt32LE(offset),0x04034b50);const start=offset+30+data.readUInt16LE(offset+26)+data.readUInt16LE(offset+28),packed=data.subarray(start,start+size),raw=method===0?packed:zlib.inflateRawSync(packed);assert.equal(crc(raw),sum);entries.set(name,raw);cursor+=46+nameLength+extra+comment;}
 assert.equal(entries.get('mimetype').toString(),'application/epub+zip');const opf=entries.get('OEBPS/book.opf').toString();assert(opf.includes('pre-paginated'));assert.equal((opf.match(/<itemref /g)||[]).length,expectedPages);
 for(const match of opf.matchAll(/href="([^"]+)"/g))assert(entries.has('OEBPS/'+match[1]),'Missing manifest target');
 let images=0;for(const [name,raw] of entries){if(name.endsWith('.xhtml'))for(const match of raw.toString().matchAll(/<img src="([^"]+)"/g))assert(entries.has('OEBPS/'+match[1]),'Missing page image');if(/\.(png|jpg)$/.test(name)){images++;assert(raw.length<1048576);if(name.endsWith('.png')){assert.equal(raw.subarray(1,4).toString(),'PNG');assert(Math.max(raw.readUInt32BE(16),raw.readUInt32BE(20))<=800);}}}
 assert.equal(images,expectedPages);
}
const csp="default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; worker-src 'self' blob:; font-src 'self' blob: data:; style-src 'self' 'unsafe-inline'; img-src 'self' blob: data:; connect-src 'self'; frame-ancestors 'none'";
const server=http.createServer((req,res)=>{let chunks=[];req.on('data',d=>chunks.push(d));req.on('end',()=>{
 try{res.setHeader('Content-Security-Policy',csp);res.setHeader('X-Content-Type-Options','nosniff');
 if(assets.has(req.url)){const a=assets.get(req.url);res.setHeader('Content-Type',a.mime);res.setHeader('Content-Encoding','gzip');res.end(a.data);return;}
 if(req.url.startsWith('/pdfjs/')){res.writeHead(404);res.end();return;}
 if(req.url==='/pdf-import.js'){res.setHeader('Content-Type','text/javascript');res.end(fs.readFileSync(path.join(root,'pdf-import.js')));return;}
 if(req.url==='/api/session'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({product:'InkReader',api:1,clock:false,token:'synthetic-test'}));return;}
 if(req.url==='/api/op'){
  assert.equal(req.headers['x-reader-token'],'synthetic-test');const op=Number(req.headers['x-reader-operation']),id=req.headers['x-reader-request'],body=Buffer.concat(chunks);requests.push(op);
  if(cached.has(id)){res.setHeader('Content-Type','application/json');res.end(cached.get(id));return;}
  const meta=op===5?{}:JSON.parse(body.toString()||'{}');let result={};
  if(op===2)result={entries:[...files].map(([name,data])=>({name,directory:false,size:data.length})),next:-1};
  if(op===3)result={total:1024**3,free:900*1024**2};
  if(op===15)heartbeats++;
  if(op===4){assert(!active);active={...meta,data:[]};}
  if(op===5){assert(active);assert.equal(body.readUInt32LE(4),crc(body.subarray(8)));const offset=active.data.reduce((n,b)=>n+b.length,0);assert.equal(body.readUInt32LE(0),offset);active.data.push(body.subarray(8));result={offset:offset+body.length-8};}
  if(op===6){assert(active);const data=Buffer.concat(active.data);assert.equal(data.length,active.size);assert.equal(crc(data),active.crc);const name=active.path.split('/').pop();files.set(name,data);fs.writeFileSync(path.join(out,name),data);active=null;}
  if(op===11){active=null;cancels++;}
  const reply=JSON.stringify(result);cached.set(id,reply);if(op===5&&!retryInjected){retryInjected=true;res.destroy();return;}res.setHeader('Content-Type','application/json');res.end(reply);return;
 }
 res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);
 }catch(e){res.writeHead(500);res.end(JSON.stringify({error:e.message}));}
});});
(async()=>{await new Promise(r=>server.listen(0,'127.0.0.1',r));let browser;const failures=[];
try{
 browser=await chromium.launch({headless:true,args:["--host-resolver-rules=MAP inkreader.test 127.0.0.1","--no-proxy-server"],...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{})});const page=await browser.newPage({viewport:{width:1100,height:1000}});page.on('pageerror',e=>failures.push(e.message));
 await page.goto('http://inkreader.test:'+server.address().port);assert.equal(await page.evaluate(()=>isSecureContext),false);await page.waitForFunction(()=>document.getElementById('message').textContent.startsWith('Connected'));
 await page.locator('#pdfoptions').evaluate(e=>e.open=true);await page.screenshot({path:path.join(out,'pdf-import-ui.png'),fullPage:true});
 await page.locator('#picker').setInputFiles(path.join(root,'test-corpus/Manga demo.pdf'));
 await page.waitForFunction(()=>!document.getElementById('cancel').offsetParent,{},{timeout:120000});
 assert.equal(files.size,1,'PDF upload failed: '+await page.locator('#message').textContent());
 const name=[...files.keys()][0];assert.equal(name,'Manga demo [PDF].epub');validateBook(files.get(name),7);assert(retryInjected);assert(heartbeats>0);assert(!active);
 // Range + two overlapping top/bottom views, preserving a Hebrew outer filename.
 const pdfBytes=fs.readFileSync(path.join(root,'test-corpus/Manga demo.pdf'));
 const converted=await page.evaluate(async({data})=>{const file=new File([Uint8Array.from(data)],'בדיקת מנגה.pdf');const result=await InkPdf.prepare(file,{layout:'panels',rtl:true,trim:false,first:3,last:3},{cancelled:()=>false,activity:async()=>{},progress:()=>{}});return {name:result.name,data:Array.from(new Uint8Array(await result.arrayBuffer()))};},{data:[...pdfBytes]});fs.writeFileSync(path.join(out,'panels.epub'),Buffer.from(converted.data));assert.equal(converted.name,'בדיקת מנגה [PDF].epub');validateBook(Buffer.from(converted.data),2);
 const checks=await page.evaluate(async({data})=>{
  const file=new File([Uint8Array.from(data)],'test.pdf'),hooks={cancelled:()=>false,activity:async()=>{},progress:()=>{}};let invalid=false,cancel=false,corrupt=false,midCancellation=false,stop=false;
  try{await InkPdf.prepare(file,{first:999},hooks);}catch(e){invalid=e.message.includes('Page range');}
  try{await InkPdf.prepare(file,{}, {...hooks,cancelled:()=>true});}catch(e){cancel=e.message==='Cancelled';}
  try{await InkPdf.prepare(new File(['not a pdf'],'bad.pdf'),{},hooks);}catch(e){corrupt=true;}
  try{await InkPdf.prepare(file,{}, {...hooks,cancelled:()=>stop,progress:text=>{if(text.startsWith('Rendering PDF page 2'))stop=true;}});}catch(e){midCancellation=e.message==='Cancelled';}
  const rtl=InkPdf.regions([0,0,800,400],'spreads',true),ltr=InkPdf.regions([0,0,800,400],'spreads',false);return {invalid,cancel,corrupt,midCancellation,rtl,ltr};
 },{data:[...pdfBytes]});assert(checks.invalid&&checks.cancel&&checks.corrupt&&checks.midCancellation);assert.equal(checks.rtl[0][0],400);assert.equal(checks.ltr[0][0],0);assert.deepEqual(failures,[]);
 const report={assets:assets.size,assetHashes:true,zipStructure:true,sevenPages:true,imageReferences:true,pdfUpload:true,chunkRetry:true,heartbeats,rangeAndPanels:true,hebrewFilename:true,invalidRange:true,cancellation:true,midConversionCancellation:true,corruptPdf:true,insecureHttpOrigin:true,pageErrors:failures};fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}finally{if(browser)await browser.close();server.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
