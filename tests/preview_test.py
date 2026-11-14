"""Headless local preview checks. No user profile or game is accessed."""
import os
from pathlib import Path
import subprocess

TEST_SCRIPT = r'''
<script>
(()=>{
 const root=document.getElementById('gakuactors-preview');
 let checks=0;const check=(value,label)=>{checks++;if(!value)throw new Error(label);};
 const q=s=>root.querySelector(s),all=s=>root.querySelectorAll(s);
 try {
  check(all('.ga-character').length===6,'initial characters');
  q('.ga-new').click();check(!q('.ga-editor').hidden,'editor opens');
  q('[name=presetName]').value='咲季 · 自定义组合';q('.ga-editor').requestSubmit();
  check(all('.ga-preset').length===2,'preset saved');
  all('.ga-add')[1].click();check(all('.ga-scene-person').length===2,'actor added');
  check(q('.ga-scene-list').textContent.includes('与主角色重叠'),'overlap state');
  q('.ga-remove').click();check(all('.ga-scene-person').length===1,'actor removed');
  q('[data-filter=special]').click();check(all('.ga-character').length===3,'special filter');
  q('[data-person=teacher]').click();check(q('.ga-person-name').textContent==='老师','teacher selected');
  q('[data-filter=all]').click();q('[data-person=hski]').click();
  if(root.clientWidth<500)q('.ga-new').click();
  const bounds=q('.ga-window').getBoundingClientRect();
  check([...all('.ga-character,.ga-preset,.ga-editor')].filter(e=>!e.hidden).every(e=>e.getBoundingClientRect().right<=bounds.right+1),'content overflow');
  document.body.setAttribute('data-preview-tests',String(checks)+' passed');
 } catch(error){document.body.setAttribute('data-preview-error',error.message);}
})();
</script>
'''


def main():
    repo = Path(__file__).absolute().parents[1]
    build = repo / 'build'
    fragment = (repo / 'preview/scene-actors.html').read_text(encoding='utf-8')
    file = build / 'preview-test.html'
    chrome = os.environ.get('GAKUACTORS_CHROME', 'C:/Program Files/Google/Chrome/Application/chrome.exe')
    for width, height, name in [(940, 1040, 'desktop'), (360, 1700, 'narrow')]:
        # Windows Chromium can enforce a wider minimum layout viewport than its
        # capture width. Constrain the product container explicitly as well.
        file.write_text('<!doctype html><html><meta charset="utf-8"><meta name="viewport" '
                        'content="width=device-width, initial-scale=1"><style>body{margin:16px;'
                        'width:' + str(width - 32) + 'px;max-width:calc(100vw - 32px);'
                        'background:light-dark(#fff,#111);color-scheme:light dark}</style><body>'
                        + fragment + TEST_SCRIPT + '</body></html>', encoding='utf-8')
        profile = build / ('preview-browser-' + str(os.getpid()) + '-' + name)
        screenshot = build / ('preview-' + name + '.png')
        command = [chrome, '--headless=new', '--disable-gpu', '--no-first-run',
                   '--disable-crash-reporter', '--disable-breakpad', '--disable-features=Crashpad', '--noerrdialogs',
                   '--no-default-browser-check', '--user-data-dir=' + str(profile),
                   '--window-size=' + str(width) + ',' + str(height), '--force-device-scale-factor=1',
                   '--hide-scrollbars', '--dump-dom', '--screenshot=' + str(screenshot), file.as_uri()]
        print('Checking ' + name + ' preview', flush=True)
        result = subprocess.run(command, capture_output=True, timeout=30,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        dom = result.stdout.decode('utf-8', 'replace')
        assert result.returncode == 0, result.stderr.decode('utf-8', 'replace')[-1000:]
        assert 'data-preview-tests="9 passed"' in dom, dom[-3000:]
        assert 'data-preview-error=' not in dom
        assert screenshot.is_file()
        print(name + ': 9 interaction/layout checks passed', flush=True)


if __name__ == '__main__':
    main()
