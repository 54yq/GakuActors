"""Searchable content labels inferred from Octo names and local game tables."""
from hashlib import sha256
import json
import os
from pathlib import Path
import re


# Bump after changing classification or index contents.
INDEX_VERSION = 1
UNKNOWN = '待识别'
TOKEN = re.compile(r'[a-z0-9]+')
COSTUME = re.compile(r'(?:^|_)([a-z0-9]{4}-[a-z]{4}-\d{4})(?=_|$)')
SONG = re.compile(r'^([a-z0-9]+)-(\d{3})(?:-([a-z0-9]+))?(?=[_-]|$)')
MUSIC_ID = re.compile(r'^music-(all|char|unit)-(?:(\w+)-)?(\d{3})(?:\s*-(inst))?$')
STAGE_SONG = re.compile(r'^([a-z]+)(\d{3})(?=-)')

IMAGE_GROUPS = {
    'adv': '剧情图片', 'chr': '角色图片', 'cos': '服装预览', 'gasha': '扭蛋图片',
    'help': '帮助图片', 'login': '登录画面', 'mov': '视频关联图片', 'notice': '公告图片',
    'produce': '培育图片', 'sd': 'Q版角色图', 'tips': '提示图片', 'tracking': '界面图片',
    'tutorial': '教程图片', 'web': '商店横幅',
}
GENERAL_IMAGES = {
    'cidol': '偶像卡图', 'csprt': '支援卡图', 'meishi': '名片素材', 'skillcard': '技能卡图',
    'icon': '图标', 'achievement': '成就图片', 'pitem': '培育道具图', 'ui': '界面图片',
    'event': '活动图片', 'live': 'Live 图片', 'home': '主页图片', 'comic4': '四格漫画',
    'shop': '商店图片', 'report': '报告图片', 'item': '道具图标', 'commu': '剧情图片',
    'gasha': '扭蛋图片', 'stamp': '印章图片', 'exchange': '兑换图片', 'pict': '图标',
    'media': '宣传图片', 'missionpass': '通行证图片', 'pdrink': '培育饮品图', 'sign': '签名图片',
    'comic': '漫画', 'memory': '回忆图片', 'cstm': '服装图片', 'mission': '任务图片',
    'campaign': '活动图片', 'produce': '培育图片',
}
CONTEXT = {
    'adv': '剧情', 'aud': '试镜', 'audition': '试镜', 'cont': '竞赛', 'dress': '换装',
    'gasha': '扭蛋', 'hif': '初星祭', 'home': '主页', 'lesson': '课程', 'live': 'Live',
    'photo': '摄影', 'prod': '培育', 'produce': '培育', 'tour': '巡演', 'tutorial': '教程',
}
LOCATIONS = {
    'classroom': '教室', 'courtyard': '中庭', 'danceroom': '舞蹈室', 'hotspring': '温泉',
    'photobooth': '照相亭', 'rooftop': '天台', 'schoolgarden': '校园花园', 'studio': '摄影棚',
    'visualroom': '形象训练室', 'vocalroom': '声乐室', 'waitingroom': '候场室',
    'library': '图书馆', 'gym': '体育馆', 'hallway': '走廊', 'entrance': '入口',
}
LIGHTING = {'morning': '清晨', 'noon': '白天', 'evening': '傍晚', 'night': '夜间'}
FORMAT = {
    '.acb': '音频 Cue Bank', '.awb': '波形音频包', '.acf': '音频配置',
    '.mp3': 'MP3', '.hca': 'HCA', '.usm': 'USM 视频',
}


def game_master_directory(desktop):
    configured = os.environ.get('GAKUACTORS_GAME_ROOT', '').strip()
    if not configured:
        try:
            configured = (desktop / 'game-root.txt').read_text(encoding='utf-8-sig').strip()
        except (OSError, UnicodeError):
            pass
    if configured:
        return desktop / configured / 'gakumas-local' / 'local-files' / 'masterTrans'
    return None


class ContentTags:
    def __init__(self, master=None):
        digest = sha256(str(INDEX_VERSION).encode('ascii'))

        def rows(filename):
            if master is None:
                return []
            try:
                raw = (master / filename).read_bytes()
                document = json.loads(raw.decode('utf-8-sig'))
            except (OSError, ValueError):
                return []
            data = document.get('data') if isinstance(document, dict) else None
            if not isinstance(data, list):
                return []
            digest.update(filename.encode('ascii'))
            digest.update(sha256(raw).digest())
            return [r for r in data if isinstance(r, dict) and isinstance(r.get('id'), str)]

        self.characters = {}
        for row in rows('Character.json'):
            name = ''.join(row[k] for k in ('lastName', 'firstName') if isinstance(row.get(k), str)).strip()
            if name:
                self.characters[row['id']] = name
        self.costumes = self.names(rows('Costume.json'))
        self.heads = self.names(rows('CostumeHead.json'))
        self.songs = {}
        for row in rows('Music.json'):
            match = MUSIC_ID.fullmatch(row['id'])
            title = row.get('title') or row.get('displayTitle')
            if match and isinstance(title, str) and title.strip():
                kind, code, number, instrumental = match.groups()
                key = (kind, code, number)
                if not instrumental or key not in self.songs:
                    self.songs[key] = title.strip().removesuffix(' [Instrumental]')
        self.signature = digest.hexdigest()

    @staticmethod
    def names(rows):
        return {r['id']: r['name'].strip() for r in rows if isinstance(r.get('name'), str) and r['name'].strip()}

    def song(self, group, number, singer=None):
        if group == 'all':
            return self.songs.get(('all', singer, number)) or self.songs.get(('all', None, number))
        if group == 'unit':
            titles = {title for (kind, _, n), title in self.songs.items() if kind == 'unit' and n == number}
            return self.songs.get(('unit', singer, number)) or (next(iter(titles)) if len(titles) == 1 else None)
        return self.songs.get(('char', group, number))

    def labels(self, name):
        raw = name.lower()
        stem, _, extension = raw.rpartition('.')
        if not stem or '/' in extension:
            stem, extension = raw, ''
        stem = stem.rsplit('/', 1)[-1]
        fields = stem.split('_')
        tokens = TOKEN.findall(stem)
        family = fields[0]
        group = fields[1] if len(fields) > 1 else ''
        subject = fields[2] if len(fields) > 2 else ''
        tags = []
        song_code = ''

        if family == 'mdl' and group == 'chr':
            part = {'body': '身体模型', 'face': '脸部模型', 'hair': '发型模型'}.get(fields[-1])
            tags.extend((part, '角色模型'))
            if part == '身体模型' and '-base-' not in stem:
                tags.append('服装模型')
        elif stem.startswith(('mdl_', 'crw_mdl_', 'fbx_mdl_', 'gmk_')):
            tags.append({'prp': '道具模型', 'env': '场景模型', 'mob': '路人模型'}.get(group, '模型部件'))
            if stem.startswith('fbx_mdl_env_'):
                tags[:1] = ['场景模型部件']
            if stem.startswith('crw_mdl_'):
                tags[:1] = ['观众模型']
            if 'microphonestand' in stem:
                tags.append('麦克风支架')
            elif 'microphone' in stem:
                tags.append('麦克风')
        elif family == 'mot' or stem.startswith(('crw_mot_', 'mog_live_')):
            if fields[-1] == 'cam' or subject.startswith('cam-'):
                tags.append('镜头动作')
            elif fields[-1] == 'layout':
                tags.append('Live 站位')
            elif stem.startswith('crw_mot_'):
                tags.append('观众动作')
            elif subject in ('chr', 'mob'):
                tags.append('表情动作' if 'facial' in tokens else '角色动作')
            elif subject == 'env':
                tags.append('场景动作')
            else:
                tags.append('动作')
            tags.append(CONTEXT.get(subject if family == 'crw' else group))
            if group == 'live':
                song_code = fields[4] if subject in ('chr', 'mob') and len(fields) > 4 else subject
                if subject in ('chr', 'mob') and family == 'mot':
                    tags.insert(0, 'Live 演出动作')
            tags.append({'lp': '循环', 'pose': '姿势'}.get(fields[-1]))
        elif family == 'img':
            if group == 'general':
                tags.append(GENERAL_IMAGES.get(subject.split('-')[0], '图片'))
                if stem.startswith('img_general_music_jacket_'):
                    tags[:1] = ['歌曲封面']
                    song_code = stem.removeprefix('img_general_music_jacket_')
                    # Jacket IDs put the singer/group before the song number for collective songs.
                    match = re.match(r'^(all|unit)-([a-z0-9]+)-(\d{3})(?:-|$)', song_code)
                    if match:
                        kind, singer, number = match.groups()
                        song_code = f'{kind}-{number}-{singer}'
                elif subject.startswith('live'):
                    song_code = self.live_image_code(fields)
            else:
                tags.append(IMAGE_GROUPS.get(group.split('-')[0], '图片'))
                if group == 'cos':
                    tags[:1] = ['发型预览' if fields[-1] == 'head' else '服装预览']
                elif group == 'chr' and 'bustup' in tokens:
                    tags[:1] = ['角色立绘']
                elif group == 'adv' and subject == 'still':
                    tags[:1] = ['剧情插图']
            if 'atlas' in tokens:
                tags.append('图集')
        elif stem.startswith('fgd_2d_adv_') and len(fields) > 3:
            tags.append({'image': '剧情前景图', 'silhouette': '角色剪影',
                         'title': '剧情标题图'}.get(fields[3], '剧情图片'))
        elif family in ('env', 'prp') or stem.startswith('crw_layout_'):
            is_live = subject == 'live'
            if family == 'prp':
                tags.append('道具')
            elif family == 'crw':
                tags.append('观众布局')
            elif is_live and group == '3d':
                tags.append('Live 舞台')
            else:
                tags.append({'2d': '2D 背景', '3d': '3D 场景'}.get(group, '场景资源'))
            if family == 'env' and is_live:
                match = STAGE_SONG.match(fields[3]) if len(fields) > 3 else None
                if match:
                    song_code = '-'.join(match.groups())
            tags.append(CONTEXT.get(subject))
            tags.extend(LOCATIONS.get(token) or LIGHTING.get(token) for token in tokens)
        elif family == 'tln':
            tags.append('Live 时间轴' if group == 'live' else '时间轴')
            tags.append(CONTEXT.get(group))
            if group == 'live':
                song_code = subject
        elif family in ('sud', 'bgm', 'se', 'vo'):
            audio = group if family == 'sud' else family
            if audio == 'music':
                if subject == 'live':
                    tags.extend(('Live 音频', '歌曲'))
                elif stem.startswith('sud_music_general_bgm_'):
                    tags.append('BGM')
                else:
                    tags.append('歌曲')
                if len(fields) > 3 and subject in ('live', 'general'):
                    song_code = fields[3]
                if 'inst' in tokens:
                    tags.append('伴奏')
            else:
                tags.append({'bgm': 'BGM', 'se': '音效', 'vo': '语音'}.get(audio, '音频'))
                tags.append(CONTEXT.get(subject))
                if audio == 'vo' and subject == 'system':
                    tags.append('系统语音')
        elif family == 'mov' or extension == 'usm':
            tags.append('Live 视频' if group == 'live' else '剧情视频' if group == 'adv' else '视频')
            tags.append(CONTEXT.get(group))
            if group == 'live':
                song_code = subject
            elif group == 'general' and subject.startswith('cidol-'):
                tags.append('偶像卡视频')
            elif group == 'general' and subject == 'gasha':
                tags.append('扭蛋视频')
        elif family == 'adv' and extension == 'txt':
            tags.append('剧情脚本')
            tags.append({'dear': '亲密度剧情', 'pevent': '培育事件', 'pstory': '培育剧情',
                         'event': '活动剧情', 'unit': '组合剧情', 'live': 'Live 剧情'}.get(group, CONTEXT.get(group)))
        elif family in ('eff', 'mef'):
            tags.append('角色特效' if family == 'mef' else '特效')
            if stem.startswith('eff_ps_'):
                tags.append('粒子特效')
            tags.append(CONTEXT.get(subject))
        elif stem.startswith(('sky_', 't_sky_')):
            tags.append('天空素材')
            tags.extend(LIGHTING.get(token) for token in tokens)
        elif stem.startswith(('tex_', 'spr_', 't_evm_')) or extension in ('png', 'jpg', 'jpeg', 'tga'):
            tags.append('图片与贴图')
        elif stem in ('shader', 'actor-shader', 'ui-shader'):
            tags.append('着色器')
        elif raw == 'musics.txt':
            tags.append('音乐列表文本')
        elif 'atlas' in tokens:
            tags.append('图集')
        elif extension in ('acb', 'awb', 'acf', 'hca', 'mp3'):
            tags.append('音频配置' if extension == 'acf' else '音频')
        else:
            tags.append(UNKNOWN)

        song = SONG.match(song_code)
        if song:
            code, number, singer = song.groups()
            tags.append(self.song(code, number, singer))
            if code in self.characters:
                tags.append(self.characters[code])
            if code == 'all' and singer in self.characters:
                tags.append(self.characters[singer])
        tags.extend(self.characters.get(token) for token in tokens)
        if 'cmmn' in tokens and family in ('mdl', 'mot', 'img'):
            tags.append('通用')
        if (family, group) in (('mdl', 'chr'), ('img', 'cos')):
            costume = COSTUME.search(stem)
            if costume:
                code = costume[1]
                name = self.heads.get(f'costume_head_{code}') if fields[-1] in ('head', 'hair') else self.costumes.get(code)
                tags.append(name)
        tags.append(FORMAT.get('.' + extension))
        return list(dict.fromkeys(tag for tag in tags if tag))

    @staticmethod
    def live_image_code(fields):
        if len(fields) < 4:
            return ''
        match = re.fullmatch(r'music-(all|char|unit)-([a-z0-9]+)-(\d{3})', fields[3])
        if match:
            kind, code, number = match.groups()
            return f'{code}-{number}' if kind == 'char' else f'{kind}-{number}-{code}'
        return ''

    def index_row(self, key, name):
        labels = self.labels(name)
        return key, json.dumps(labels, ensure_ascii=False), '\n'.join(labels).casefold()
