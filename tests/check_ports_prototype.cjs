// Offline headless QA for the design fragment; does not exercise any SSH/socket implementation.
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const { pathToFileURL } = require('url');
const runtime = 'C:/Users/JOKER/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = require(path.join(runtime, 'playwright'));
const root = path.resolve(__dirname, '..');
const report = path.join(root, 'test-results/0.1.10');
const preview = path.join(report, 'ports-preview.html');
const fragment = 'C:/Users/JOKER/.codex/visualizations/2026/11/02/01a19a9e-820c-7312-b18a-74e099d3e831/rabbitshell-ports-panel.html';
const checks = [], errors = [], unexpectedRequests = [], dialogObservations = [], consoleErrors = [];
const source = fs.readFileSync(fragment, 'utf8');
assert(!/<(?:!doctype|html|head|body)\b/i.test(source));
assert(!/\b(?:fetch|XMLHttpRequest|WebSocket)\b|<script[^>]+src=/i.test(source));
assert(!source.includes('\\"'));
assert(Buffer.byteLength(source) < 1000000);
function passed(name) { checks.push(name); console.log('PASS ' + name); }
(async () => {
  const browser = await chromium.launch({headless:true, executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',
    args:['--disable-background-networking','--disable-component-update','--no-first-run']});
  try {
    const context = await browser.newContext({viewport:{width:800,height:1000}, colorScheme:'light'});
    await context.route('https://**/*', async route => {
      const url=route.request().url();
      if(url.startsWith('https://unpkg.com/lucide@'))
        return route.fulfill({contentType:'application/javascript',body:fs.readFileSync(path.join(runtime,'lucide/dist/umd/lucide.js'))});
      // The standalone wrapper's optional tooltip helpers are inert in this offline harness.
      if(url.startsWith('https://unpkg.com/@floating-ui/'))return route.fulfill({contentType:'application/javascript',body:''});
      unexpectedRequests.push(url); await route.abort();
    });
    const page=await context.newPage(); page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{if(message.type()==='error')consoleErrors.push(message.text());});
    await page.goto(pathToFileURL(preview).href);
    const frame=page.frameLocator('iframe'); const app=frame.locator('#rs-ports-preview');
    await app.waitFor();
    const getFrame=()=>page.frames().find(f=>f!==page.mainFrame());
    await frame.locator('[data-action="maximize"] svg').waitFor();
    const row=id=>frame.locator('tr[data-rule="'+id+'"]');
    const click=action=>frame.locator('[data-action="'+action+'"]');
    const input=field=>frame.locator('[data-draft] input[data-field="'+field+'"]');
    const draft=()=>frame.locator('[data-draft]');
    const toggle=id=>row(id).locator('[data-toggle]');
    const pendingDone=async id=>{await row(id).locator('[data-toggle]:not(:disabled)').waitFor();};
    const menu=async(id,action)=>{await row(id).locator('[data-action="menu"]').click();await frame.locator('.rs-menu [data-action="'+action+'"]').click();};
    const submitDialog=async (name,enter=false)=>{
      const inspect=()=>frame.locator('dialog').evaluate(dialog=>({open:dialog.open,error:dialog.querySelector('[data-dialog-error]')?.textContent||'',
        fields:Array.from(dialog.querySelectorAll('input')).map(input=>({name:input.name,value:input.value})),buttonType:dialog.querySelector('[data-dialog-submit]').type}));
      const before=await inspect();assert.equal(before.open,true);
      if(enter)await frame.locator('dialog [name="remoteAddress"]').press('Enter');
      else await frame.locator('[data-dialog-submit]').click();
      const after=await inspect();dialogObservations.push({name,before,after});
      fs.writeFileSync(path.join(report,'ports-dialog-observations.json'),JSON.stringify({dialogObservations,consoleErrors},null,2));
      assert.equal(after.open,false,'Dialog save did not close: '+JSON.stringify(after));
    };
    assert.deepEqual(await frame.locator('th').allTextContents(),['本地端口','远端端口','启用']);
    assert.equal(await frame.locator('tbody tr').count(),2);
    assert.equal(await frame.locator('[data-toggle]:checked').count(),0);
    assert.equal(await click('add').count(),1);passed('three columns, two stopped rules, single add entry');
    // Host is light even while the product remains Dracula.
    const caption=await getFrame().evaluate(()=>{
      const label=document.querySelector('.rs-caption'),probe=document.createElement('span');probe.style.color='var(--foreground)';label.append(probe);
      const colors=[getComputedStyle(label).color,getComputedStyle(probe).color];probe.remove();return colors;
    });assert.equal(caption[0],caption[1]);passed('outside caption uses light host foreground');
    await click('add').click();await input('remote').fill('8081');assert.equal(await input('local').inputValue(),'8081');
    await input('local').fill('18081');await input('remote').fill('8082');assert.equal(await input('local').inputValue(),'18081');
    await input('local').press('Tab');assert.equal(await draft().count(),1);
    await input('remote').press('Enter');assert.equal(await draft().count(),0);
    const added=await frame.locator('tr[data-rule]').last().getAttribute('data-rule');
    assert.equal(await toggle(added).isChecked(),false);passed('remote default, manual local preserved, same-row Tab, Enter saves stopped');
    await click('add').click();await frame.locator('.rs-file').click();assert.equal(await draft().count(),0);
    await click('add').click();await input('remote').fill('3306');await frame.locator('.rs-file').click();
    assert.equal(await frame.locator('tbody tr').count(),4);assert.equal(await frame.locator('[data-toggle]:checked').count(),0);
    passed('empty blur cancels, valid outside click saves stopped');
    await click('add').click();await input('local').fill('20000');
    for(const value of ['0','1.2','abc','65536','-1']){
      await input('remote').fill(value);await input('remote').press('Enter');assert.equal(await draft().count(),1);
      assert.match(await frame.locator('[data-error="remote"]').innerText(),/1–65535/);
    }
    await input('remote').fill('');await frame.locator('.rs-file').click();assert.match(await frame.locator('[data-error="remote"]').innerText(),/请输入/);
    await frame.locator('#rs-terminal-tab').click();assert.equal(await frame.locator('#rs-terminal-tab').getAttribute('aria-selected'),'true');
    await frame.locator('#rs-ports-tab').click();assert.equal(await input('local').inputValue(),'20000');assert.equal(await input('remote').inputValue(),'');
    await input('remote').press('Escape');assert.equal(await draft().count(),0);
    passed('invalid values retained, partial draft survives tab switch, Escape cancels');
    await toggle('preview-web').click();assert.equal(await toggle('preview-web').isDisabled(),true);assert.equal(await toggle('preview-web').isChecked(),false);
    await pendingDone('preview-web');assert.equal(await toggle('preview-web').isChecked(),true);
    assert.match(await frame.locator('[data-announcement]').innerText(),/127.0.0.1:18080.*127.0.0.1:8080/);
    await click('add').click();await input('remote').fill('9099');await input('local').fill('18080');await input('remote').press('Enter');
    const duplicate=await frame.locator('tr[data-rule]').last().getAttribute('data-rule');
    await toggle(duplicate).click();await pendingDone(duplicate);assert.equal(await toggle(duplicate).isChecked(),false);
    assert.match(await row(duplicate).innerText(),/18080 已被占用/);assert.equal(await row(duplicate).locator('[data-field="local"]').innerText(),'18080');
    passed('pending toggle disabled, success address, duplicate port fails off without renumber');
    // A is actively running with a draft; restarting B must retain A's draft.
    await toggle('preview-db').click();await pendingDone('preview-db');
    await row('preview-web').locator('[data-field="remote"]').click();await input('remote').fill('8181');
    await frame.locator('.rs-file').click();assert.equal(await input('remote').inputValue(),'8181');assert.equal(await toggle('preview-web').isChecked(),true);
    await menu('preview-db','advanced');await frame.locator('[name="remoteAddress"]').fill('10.0.0.12');
    assert.equal(await frame.locator('[data-dialog-submit]').innerText(),'应用并重启');await submitDialog('restart B while editing A');
    assert.equal(await toggle('preview-db').isDisabled(),true);
    await pendingDone('preview-db');assert.equal(await input('remote').inputValue(),'8181');assert.equal(await draft().count(),1);
    assert.match(await row('preview-db').innerText(),/10\.0\.0\.12:5432/);
    await click('cancel').click();assert.equal(await row('preview-web').locator('[data-field="remote"]').innerText(),'8080');
    await row('preview-web').locator('[data-field="remote"]').click();await input('remote').fill('8181');await click('apply').click();
    await pendingDone('preview-web');assert.equal(await row('preview-web').locator('[data-field="remote"]').innerText(),'8181');assert.equal(await toggle('preview-web').isChecked(),true);
    passed('running edit never blur-applies; B restart preserves A draft; explicit apply restarts only A');
    await menu('preview-web','delete');assert.equal(await frame.locator('dialog[open]').count(),1);
    await click('cancel-dialog').click();assert.equal(await toggle('preview-web').isChecked(),true);
    await menu('preview-web','delete');await frame.locator('[data-dialog-submit]').click();assert.equal(await row('preview-web').count(),0);
    passed('running delete requires one confirmation; cancel preserves state');
    // Valid new draft commits before another row's menu, whose anchor has been replaced.
    await click('add').click();await input('remote').fill('6000');await row(added).locator('[data-action="menu"]').click();
    assert.equal(await draft().count(),0);
    const menuBox=await frame.locator('.rs-menu').boundingBox(),anchorBox=await row(added).locator('[data-action="menu"]').boundingBox();
    assert(menuBox && anchorBox && Math.abs(menuBox.x+menuBox.width-anchorBox.x-anchorBox.width)<30);
    await frame.locator('.rs-menu [data-action="advanced"]').click();
    await frame.locator('[name="localAddress"]').fill('::1');await frame.locator('[name="remoteAddress"]').fill('db.internal');
    await frame.locator('[name="autoStart"]').check();await submitDialog('save advanced via Enter',true);
    assert.match(await row(added).innerText(),/\[::1\]:18081/);assert.match(await row(added).innerText(),/db.internal:8082/);assert.equal(await toggle(added).isChecked(),false);
    passed('committed-draft menu reanchors; advanced non-default addresses visible and autostart does not start');
    await getFrame().evaluate(()=>Object.defineProperty(navigator,'clipboard',{configurable:true,value:{writeText:async text=>{window.__copied=text;}}}));
    await menu(added,'copy');assert.equal(await getFrame().evaluate(()=>window.__copied),'[::1]:18081');
    await getFrame().evaluate(()=>Object.defineProperty(navigator,'clipboard',{configurable:true,value:undefined}));
    await menu(added,'copy');assert.equal(await frame.locator('dialog input').inputValue(),'[::1]:18081');await frame.locator('[data-dialog-submit]').click();
    passed('non-web local address copies without URL, unavailable clipboard offers selected text');
    const terminalText=await frame.locator('#rs-terminal-pane').textContent();
    const iconBefore=await click('maximize').locator('svg').innerHTML(),colorBefore=await click('maximize').evaluate(e=>getComputedStyle(e).color);
    const ruleCount=await frame.locator('tbody tr').count();await click('maximize').click();
    assert.equal(await click('maximize').getAttribute('aria-pressed'),'true');assert.equal(await click('maximize').locator('svg').innerHTML(),iconBefore);
    assert.equal(await click('maximize').evaluate(e=>getComputedStyle(e).color),colorBefore);
    await frame.locator('#rs-terminal-tab').click();assert.equal(await click('maximize').getAttribute('aria-pressed'),'true');
    await click('hide').click();assert.equal(await click('show').isVisible(),true);await click('show').click();
    assert.equal(await frame.locator('#rs-terminal-tab').getAttribute('aria-selected'),'true');assert.equal(await frame.locator('#rs-terminal-pane').textContent(),terminalText);
    await frame.locator('#rs-ports-tab').click();assert.equal(await frame.locator('tbody tr').count(),ruleCount);
    passed('same maximize glyph and ink; switching/hiding/showing preserve rules and terminal display');
    while(await frame.locator('tr[data-rule]').count()){
      const id=await frame.locator('tr[data-rule]').first().getAttribute('data-rule');await menu(id,'delete');
      if(await frame.locator('dialog[open]').count())await frame.locator('[data-dialog-submit]').click();
    }
    assert.equal(await frame.locator('.rs-empty[data-empty]').innerText(),'暂无端口转发');assert.equal(await click('add').count(),1);
    await click('add').click();await input('remote').fill('8080');await frame.locator('[data-toggle="draft"]').click();
    const explicit=await frame.locator('tr[data-rule]').first().getAttribute('data-rule');await pendingDone(explicit);assert.equal(await toggle(explicit).isChecked(),true);
    passed('delete to empty, single add entry, draft toggle explicitly saves and starts');
    // Restore the default mock state through the documented host state event for visual evidence.
    const screenshotState={version:1,theme:'dracula',tab:'ports',hidden:false,maximized:false,rules:[
      {id:'shot-web',local:18080,remote:8080,localAddress:'127.0.0.1',remoteAddress:'127.0.0.1',autoStart:false,enabled:false},
      {id:'shot-db',local:15432,remote:5432,localAddress:'127.0.0.1',remoteAddress:'127.0.0.1',autoStart:false,enabled:false}
    ]};
    const restoreEvidence=()=>getFrame().evaluate(state=>window.dispatchEvent(new CustomEvent('openai:set_globals',{detail:{globals:{widgetState:{privateContent:state}}}})),screenshotState);
    await restoreEvidence();
    assert.equal(await row('shot-db').count(),1);
    await app.screenshot({path:path.join(report,'ports-dark.png')});
    screenshotState.theme='light';await restoreEvidence();
    assert.equal(await app.getAttribute('data-theme'),'light');await app.screenshot({path:path.join(report,'ports-light.png')});
    // A 352px outer viewport gives the renderer's iframe an actual 320px content width.
    await page.setViewportSize({width:352,height:1100});
    const width=await getFrame().evaluate(()=>document.documentElement.clientWidth);assert.equal(width,320);
    const overflow=()=>getFrame().evaluate(()=>{
      const root=document.getElementById('rs-ports-preview');return {client:document.documentElement.clientWidth,scroll:document.documentElement.scrollWidth,root:root.scrollWidth,width:root.clientWidth};
    });
    let sizes=await overflow();assert(sizes.scroll<=sizes.client && sizes.root<=sizes.width);
    await app.screenshot({path:path.join(report,'ports-320.png')});
    await click('add').click();await input('remote').fill('invalid');await input('remote').press('Enter');
    sizes=await overflow();assert(sizes.scroll<=sizes.client && sizes.root<=sizes.width);await app.screenshot({path:path.join(report,'ports-320-validation.png')});
    await input('remote').press('Escape');await menu('shot-db','advanced');
    sizes=await overflow();assert(sizes.scroll<=sizes.client && sizes.root<=sizes.width);
    await app.screenshot({path:path.join(report,'ports-320-advanced.png')});await click('cancel-dialog').click();
    passed('Dracula and GitHub Light screenshots; 320px table, draft errors, advanced dialog fit');
    assert.deepEqual(errors,[]);assert.deepEqual(consoleErrors,[]);assert.deepEqual(unexpectedRequests,[]);
    passed('zero runtime errors; no outbound network; wrapper Lucide fulfilled from local bundled library');
    fs.writeFileSync(path.join(report,'ports-prototype-report.json'),JSON.stringify({passed:checks.length,failed:0,checks,errors,consoleErrors,unexpectedRequests,dialogObservations,
      viewport320:width,fragmentBytes:Buffer.byteLength(source),scope:'Headless Chrome in fresh profile, skill-rendered sandbox iframe; local host-library substitution; no visible desktop or SSH/socket activity.'},null,2));
    await context.close();
  } finally {await browser.close();}
})().catch(error=>{console.error(error);fs.writeFileSync(path.join(report,'ports-prototype-failure.txt'),String(error.stack));process.exitCode=1;});
