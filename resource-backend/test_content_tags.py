from contextlib import closing
import json
import sqlite3
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from catalog import Catalog


class ContentTagsTest(unittest.TestCase):
    def setUp(self):
        work = TemporaryDirectory()
        self.addCleanup(work.cleanup)
        self.root = Path(work.name)
        self.master = self.root / 'game' / 'gakumas-local' / 'local-files' / 'masterTrans'
        self.master.mkdir(parents=True)
        self.write_table('Character', {'id': 'hski', 'lastName': '花海', 'firstName': '咲季'})
        self.write_table('CostumeHead', {'id': 'costume_head_hski-cstm-0000', 'name': '初始发型'})
        self.write_table('Music', {'id': 'music-char-hski-001', 'title': 'Fighting My Way'})
        game_root = patch.dict('os.environ', {'GAKUACTORS_GAME_ROOT': str(self.root / 'game')})
        game_root.start()
        self.addCleanup(game_root.stop)

    def write_table(self, table, *rows):
        (self.master / f'{table}.json').write_text(json.dumps({'data': rows}), encoding='utf-8')

    def import_names(self, catalog, *names):
        entries = [dict(section=2, id=i, name=name, size=12, md5='a' * 32,
                        object_name='asset', dependencies=[]) for i, name in enumerate(names)]
        data = dict(format='gakumas-resource-catalog', schema=1, revision=1, entries=entries)
        return catalog.import_bytes(json.dumps(data).encode())

    def test_local_tags_and_intersection_with_raw_name_and_category(self):
        catalog = Catalog(self.root / 'library')
        self.import_names(catalog, 'mdl_chr_hski-cstm-0000_hair', 'env_3d_live_hski001-00-noon',
                          'tln_live_hski-001', 'adv_dear_hski_001.txt', 'unrecognized')
        hit = catalog.search('花海咲季 发型 cstm', 'model')['items']
        self.assertEqual([entry['key'] for entry in hit], ['a:0'])
        self.assertEqual(hit[0]['tags'], ['发型模型', '角色模型', '花海咲季', '初始发型'])
        self.assertEqual(catalog.search('live 舞台 FIGHTING')['total'], 1)
        self.assertEqual(catalog.search('fighting MY way')['total'], 2)
        self.assertEqual(catalog.get('a:3')['tags'], ['剧情脚本', '亲密度剧情', '花海咲季'])
        self.assertEqual(catalog.search('待识别')['total'], 1)
        self.assertNotIn('tags', catalog.export_json()['entries'][0])

    def test_backfill_then_reimport_and_refresh_local_names(self):
        root = self.root / 'library'
        catalog = Catalog(root)
        self.import_names(catalog, 'sud_vo_system_hski_001.acb')
        with closing(sqlite3.connect(catalog.db)) as db, db:
            db.execute('DROP TABLE entry_content_tags')
            db.execute("DELETE FROM meta WHERE key='content_tags'")
        catalog = Catalog(root)
        self.assertEqual(catalog.search('系统语音 花海咲季')['total'], 1)
        self.import_names(catalog, 'mov_live_hski-001_00.usm')
        self.assertEqual(catalog.search('系统语音')['total'], 0)
        self.assertEqual(catalog.search('Live 视频')['total'], 1)
        self.write_table('Music', {'id': 'music-char-hski-001', 'title': 'Updated Song'})
        catalog = Catalog(root)
        self.assertEqual(catalog.search('updated song')['total'], 1)
        self.assertEqual(catalog.search('fighting')['total'], 0)


if __name__ == '__main__':
    unittest.main()
