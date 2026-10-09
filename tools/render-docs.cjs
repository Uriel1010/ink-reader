const fs=require('fs'),path=require('path'),http=require('http');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'ebook-reader/ReaderWebAssets.h'),'utf8');
const html=source.split('R"INKWEB(')[1].split(')INKWEB"')[0];
const files=[{name:'Books',directory:true,size:0},{name:'Pictures',directory:true,size:0},{name:'A Quiet Morning.epub',directory:false,size:248320},{name:'קריאה נעימה.txt',directory:false,size:18240},{name:'Garden light.jpg',directory:false,size:96850}];
(async()=>{
const server=http.createServer((req,res)=>{
 res.setHeader('Cache-Control','no-store');
 if(req.url==='/pdf-import.js'){res.setHeader('Content-Type','text/javascript');res.end(fs.readFileSync(path.join(root,'ebook-reader/pdf-import.js')));return;}
 if(req.url.startsWith('/api/')){res.setHeader('Content-Type','application/json');let result={};if(req.url==='/api/session')result={product:'InkReader',api:1,token:'synthetic-preview',clock:true};else {const op=Number(req.headers['x-reader-operation']);if(op===2)result={entries:files,next:-1};if(op===3)result={total:16*1024**3,free:12*1024**3};}res.end(JSON.stringify(result));}
 else {res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try{
 browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{})});
 for(const [name,width,height] of [['desktop',1200,1040],['mobile',390,1100]]){
 const page=await browser.newPage({viewport:{width,height},deviceScaleFactor:1});
 await page.goto('http://127.0.0.1:'+server.address().port);
 await page.locator('.file').first().waitFor();
 await page.screenshot({path:path.join(root,'docs/images/file-manager-'+name+'.png'),fullPage:true});
 await page.close();
 }
 console.log('Rendered actual browser UI with synthetic fixtures.');
}finally{if(browser)await browser.close();server.close();}
})().catch(e=>{console.error(e.message);process.exitCode=1;});
