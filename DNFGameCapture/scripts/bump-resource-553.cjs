// UTF-16LE .rc is intentionally edited through an approved command, preserving its encoding/BOM.
const fs=require('fs'),crypto=require('crypto'),path=require('path');
const file=path.resolve(__dirname,'../DNFGameCapture.rc');const bytes=fs.readFileSync(file);
const hash=crypto.createHash('sha256').update(bytes).digest('hex');
if(hash!=='a1d510106f4b8e9786d80c476fce4aaa996750e1b71e54d3d093f0ed25942343')throw Error('Resource changed since inspection; reread before editing.');
if(bytes[0]!==255||bytes[1]!==254)throw Error('Expected UTF-16LE BOM');
let text=bytes.toString('utf16le');
for(const [old,next] of [[' FILEVERSION 5,5,2,0',' FILEVERSION 5,5,3,0'],[' PRODUCTVERSION 5,5,2,0',' PRODUCTVERSION 5,5,3,0'],['VALUE "FileVersion", "5.5.2.0"','VALUE "FileVersion", "5.5.3.0"'],['VALUE "ProductVersion", "5.5.2.0"','VALUE "ProductVersion", "5.5.3.0"']]){
 if(text.split(old).length!==2)throw Error('Version anchor not unique: '+old);text=text.replace(old,next);
}
fs.writeFileSync(file,Buffer.from(text,'utf16le'));console.log('RC file/product version = 5.5.3.0; original UTF-16LE BOM preserved.');
