// A TEST ONLY trusted-native-broker simulation that forwards JS wrapper calls
// through a PRIVATE parent/child pipe. This is not WebView2/CEF or NW-014.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {pathToFileURL} from 'node:url';

const [wrapper, python, bindingPath, testPath] = process.argv.slice(2);
const child = spawn(python, ['-m','xytron.host_process','--plugin-id','example_py',
  '--module','python_fixture','--class','Player','--owner','session_1'],
  {env:{...process.env,PYTHONPATH: [bindingPath,testPath].join(process.platform==='win32'?';':':')},
   stdio:['pipe','pipe','pipe']});
const output = createInterface({input:child.stdout});
const pending = new Map();
let id = 0;
let stderr = '';
child.stderr.on('data',d=>{stderr += d.toString();});
output.on('line',line=>{
  try {
    const message = JSON.parse(line);
    const handler = pending.get(message.id);
    if (!handler) throw Error('Unexpected child response');
    pending.delete(message.id);
    if (message.ok) handler.resolve(message.result);
    else handler.reject(Error('IPC ' + message.error));
  } catch (err) {
    for(const handler of pending.values()) handler.reject(err);
    pending.clear();
  }
});
globalThis.xytron = {invoke(operation,...args) {
  const requestId = ++id;
  return new Promise((resolve,reject)=>{
    pending.set(requestId,{resolve,reject});
    child.stdin.write(JSON.stringify({id:requestId, operation, args})+'\n');
  });
}};
try {
  const {Player} = await import(pathToFileURL(wrapper).href);
  const player = await Player.create('Ada');
  assert.equal(await player.hit(9),91);
  assert.equal(await player.getHealth(),91);
  await player.setHealth(33);
  assert.equal(await player.getHealth(),33);
  await player.dispose();
  await player.dispose();
  assert.throws(()=>player.hit(3),/already disposed/);
  child.stdin.end();
  const rc = await new Promise((resolve,reject)=>{
    child.on('error',reject);child.on('exit',resolve);
  });
  assert.equal(rc,0,stderr);
  console.log('PASS: generated JS to real Python subprocess');
} catch(err) {
  console.error(err);
  child.kill();
  process.exitCode=1;
}
