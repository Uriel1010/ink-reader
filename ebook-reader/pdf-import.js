/* Original Ink Reader PDF import. PDF.js is loaded locally, never from a CDN. */
'use strict';
window.InkPdf=(()=>{
 const LIMIT_INPUT=128*1024*1024,LIMIT_OUTPUT=256*1024*1024,MAX_PAGES=1000;
 const encoder=new TextEncoder();
 const table=Array.from({length:256},(_,n)=>{for(let k=0;k<8;k++)n=(n>>>1)^((n&1)?0xedb88320:0);return n>>>0;});
 const xml=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
 function crc(data){let value=0xffffffff;for(const b of data)value=(value>>>8)^table[(value^b)&255];return (~value)>>>0;}
 class Zip {
  constructor(){this.parts=[];this.directory=[];this.offset=0;}
  async add(name,blob){const data=new Uint8Array(await blob.arrayBuffer()),key=encoder.encode(name),sum=crc(data);if(this.offset+data.length>LIMIT_OUTPUT)throw Error('Converted book exceeds 256 MiB. Import a smaller page range.');
   const local=new Uint8Array(30+key.length),v=new DataView(local.buffer);v.setUint32(0,0x04034b50,true);v.setUint16(4,20,true);v.setUint16(6,0x800,true);v.setUint16(12,0x21,true);v.setUint32(14,sum,true);v.setUint32(18,data.length,true);v.setUint32(22,data.length,true);v.setUint16(26,key.length,true);local.set(key,30);
   const central=new Uint8Array(46+key.length),c=new DataView(central.buffer);c.setUint32(0,0x02014b50,true);c.setUint16(4,20,true);c.setUint16(6,20,true);c.setUint16(8,0x800,true);c.setUint16(14,0x21,true);c.setUint32(16,sum,true);c.setUint32(20,data.length,true);c.setUint32(24,data.length,true);c.setUint16(28,key.length,true);c.setUint32(42,this.offset,true);central.set(key,46);
   this.parts.push(local,blob);this.directory.push(central);this.offset+=local.length+data.length;
  }
  async text(name,text){await this.add(name,new Blob([text]));}
  finish(name){const size=this.directory.reduce((n,p)=>n+p.length,0);if(this.offset+size+22>LIMIT_OUTPUT)throw Error('Converted book exceeds 256 MiB. Import a smaller range.');const end=new Uint8Array(22),v=new DataView(end.buffer);v.setUint32(0,0x06054b50,true);v.setUint16(8,this.directory.length,true);v.setUint16(10,this.directory.length,true);v.setUint32(12,size,true);v.setUint32(16,this.offset,true);return new File([...this.parts,...this.directory,end],name,{type:'application/epub+zip'});}
 }
 function crop(canvas,trim){if(!trim)return [0,0,canvas.width,canvas.height];const w=canvas.width,h=canvas.height,p=canvas.getContext('2d').getImageData(0,0,w,h).data;let x0=w,y0=h,x1=-1,y1=-1;
  for(let y=0;y<h;y++)for(let x=0;x<w;x++){const i=4*(y*w+x);if(Math.min(p[i],p[i+1],p[i+2])<235){x0=Math.min(x0,x);x1=Math.max(x1,x);y0=Math.min(y0,y);y1=Math.max(y1,y);}}
  if(x1<0)return [0,0,w,h];const padding=6;x0=Math.max(0,x0-padding);y0=Math.max(0,y0-padding);x1=Math.min(w-1,x1+padding);y1=Math.min(h-1,y1+padding);return [x0,y0,x1-x0+1,y1-y0+1];
 }
 function regions(rect,layout,rtl){const [x,y,w,h]=rect;
  if(layout==='panels'){const overlap=Math.max(2,Math.round(h*.025)),half=Math.ceil(h/2);return [[x,y,w,Math.min(h,half+overlap)],[x,y+Math.max(0,half-overlap),w,h-Math.max(0,half-overlap)]];}
  if(layout==='spreads'&&w/h>1.2){const half=Math.floor(w/2),parts=[[x,y,half,h],[x+half,y,w-half,h]];return rtl?parts.reverse():parts;}
  return [rect];
 }
 async function image(canvas,rect){const [x,y,w,h]=rect,scale=Math.min(1,800/Math.max(w,h));const output=document.createElement('canvas');output.width=Math.max(1,Math.round(w*scale));output.height=Math.max(1,Math.round(h*scale));const ctx=output.getContext('2d');ctx.fillStyle='white';ctx.fillRect(0,0,output.width,output.height);ctx.drawImage(canvas,x,y,w,h,0,0,output.width,output.height);
  try {const lossless=await new Promise(resolve=>output.toBlob(resolve,'image/png'));if(lossless&&lossless.size<1048576)return {blob:lossless,width:output.width,height:output.height,ext:'png',mime:'image/png'};for(const quality of [.94,.88,.8,.7]){const blob=await new Promise(resolve=>output.toBlob(resolve,'image/jpeg',quality));if(blob&&blob.size<1048576)return {blob,width:output.width,height:output.height,ext:'jpg',mime:'image/jpeg'};}throw Error('A rendered page exceeds the image limit.');}finally{output.width=output.height=1;}
 }
 async function prepare(file,options,hooks){
  if(file.size>LIMIT_INPUT)throw Error('PDF exceeds 128 MiB. Select a smaller PDF.');
  if(!file.size)throw Error('PDF is empty.');
  if(!Promise.withResolvers)Promise.withResolvers=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};};
  let pdf,task,render,timer,heartbeat,heartbeatError;const started=Date.now();
  const check=()=>{if(hooks.cancelled())throw Error('Cancelled');if(heartbeatError)throw Error('Reader disconnected during PDF preparation. Reconnect and try again.');if(Date.now()-started>600000)throw Error('PDF preparation timed out. Import a smaller page range.');};
  try{
   hooks.progress('Loading local PDF renderer',0,1);
   const engine=await import('/pdfjs/6.4.299/pdf.mjs');check();engine.GlobalWorkerOptions.workerSrc='/pdfjs/6.4.299/pdf.worker.mjs';
   task=engine.getDocument({data:new Uint8Array(await file.arrayBuffer()),isEvalSupported:false,enableXfa:false,enableScripting:false,useWasm:false,wasmUrl:'/pdfjs/6.4.299/wasm/',standardFontDataUrl:'/pdfjs/6.4.299/standard_fonts/',cMapUrl:'/pdfjs/6.4.299/cmaps/',cMapPacked:true,stopAtErrors:true,maxImageSize:32000000,canvasMaxAreaInBytes:16000000});
   task.onPassword=(update,reason)=>{const value=prompt(reason===2?'Incorrect PDF password. Try again:':'PDF password (not saved):');if(value===null){task.destroy();return;}update(value);};
   timer=setInterval(()=>{if(hooks.cancelled()||Date.now()-started>600000||heartbeatError){if(render)render.cancel();if(task)task.destroy();}},200);
   heartbeat=setInterval(()=>hooks.activity().catch(e=>{heartbeatError=e;}),20000);
   await hooks.activity();pdf=await task.promise;check();
   const first=Number(options.first||1),last=options.last?Number(options.last):pdf.numPages;
   if(!Number.isInteger(first)||!Number.isInteger(last)||first<1||last<first||last>pdf.numPages)throw Error('Page range must be within 1-'+pdf.numPages+'.');
   if(last-first+1>MAX_PAGES)throw Error('Import at most 1000 source pages at a time.');
   const title=file.name.replace(/\.pdf$/i,''),zip=new Zip(),entries=[];
   await zip.text('mimetype','application/epub+zip');
   await zip.text('META-INF/container.xml','<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>');
   for(let number=first;number<=last;number++){
    check();hooks.progress('Rendering PDF page '+number+' / '+pdf.numPages,number-first,last-first+1);await hooks.activity();
    const page=await pdf.getPage(number),view=page.getViewport({scale:1}),scale=Math.min(2,1200/Math.max(view.width,view.height));
    const viewport=page.getViewport({scale}),canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.ceil(viewport.width));canvas.height=Math.max(1,Math.ceil(viewport.height));
    try{
     render=page.render({canvasContext:canvas.getContext('2d'),viewport,background:'rgb(255,255,255)'});await render.promise;render=null;check();
     const parts=regions(crop(canvas,options.trim),options.layout,options.rtl);
     for(let part=0;part<parts.length;part++){check();const result=await image(canvas,parts[part]),id='p'+String(entries.length+1).padStart(5,'0'),label='PDF page '+number+(parts.length>1?' - '+(options.layout==='panels'?(part?'bottom':'top'):(options.rtl?(part?'left':'right'):(part?'right':'left'))):'');
      await zip.add('OEBPS/'+id+'.'+result.ext,result.blob);await zip.text('OEBPS/'+id+'.xhtml','<?xml version="1.0" encoding="UTF-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>'+xml(label)+'</title><meta name="viewport" content="width='+result.width+',height='+result.height+'"/></head><body style="margin:0"><img src="'+id+'.'+result.ext+'" alt="'+xml(label)+'"/></body></html>');entries.push({id,label,ext:result.ext,mime:result.mime});
     }
    }finally{canvas.width=canvas.height=1;page.cleanup();}
   }
   const manifest=entries.map((e,i)=>'<item id="'+e.id+'" href="'+e.id+'.xhtml" media-type="application/xhtml+xml"/><item id="'+e.id+'img" href="'+e.id+'.'+e.ext+'" media-type="'+e.mime+'"'+(i===0?' properties="cover-image"':'')+'/>').join('');
   await zip.text('OEBPS/nav.xhtml','<?xml version="1.0" encoding="UTF-8"?><html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Pages</title></head><body><nav epub:type="toc"><ol>'+entries.map(e=>'<li><a href="'+e.id+'.xhtml">'+xml(e.label)+'</a></li>').join('')+'</ol></nav></body></html>');
   await zip.text('OEBPS/book.opf','<?xml version="1.0" encoding="UTF-8"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id" prefix="rendition: http://www.idpf.org/vocab/rendition/#"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">inkpdf-'+Date.now()+'</dc:identifier><dc:title>'+xml(title)+'</dc:title><dc:creator>PDF import</dc:creator><dc:language>und</dc:language><meta property="dcterms:modified">'+new Date().toISOString().replace(/\.\d+Z$/,'Z')+'</meta><meta property="rendition:layout">pre-paginated</meta><meta property="rendition:spread">none</meta></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'+manifest+'</manifest><spine page-progression-direction="'+(options.rtl?'rtl':'ltr')+'">'+entries.map(e=>'<itemref idref="'+e.id+'"/>').join('')+'</spine></package>');
   check();hooks.progress('PDF ready - '+entries.length+' reading pages',1,1);return zip.finish(title+' [PDF].epub');
  }catch(error){if(hooks.cancelled())throw Error('Cancelled');throw Error(error.message||'PDF could not be rendered.');}
  finally{clearInterval(timer);clearInterval(heartbeat);if(render)render.cancel();if(task)await task.destroy().catch(()=>{});}
 }
 return {prepare,regions,Zip};
})();
