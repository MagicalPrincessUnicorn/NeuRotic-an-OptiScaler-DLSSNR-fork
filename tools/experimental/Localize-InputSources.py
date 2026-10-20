"""Append only this feature's authored strings; retain all existing translations."""
import json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
locales=['de','es','fr','ja','ko','pl','pt-BR','ru','zh-CN']
# German, Spanish, French, Japanese, Korean, Polish, Brazilian Portuguese,
# Russian and Simplified Chinese. Technical provider names remain unchanged.
rows=[
('depth','Depth',['Tiefe','Profundidad','Profondeur','深度','깊이','Głębia','Profundidade','Глубина','深度']),
('motion','Motion vectors',['Bewegungsvektoren','Vectores de movimiento','Vecteurs de mouvement','モーションベクトル','모션 벡터','Wektory ruchu','Vetores de movimento','Векторы движения','运动矢量']),
('auto','Automatic',['Automatisch','Automático','Automatique','自動','자동','Automatycznie','Automático','Автоматически','自动']),
('native','Native',['Nativ','Nativo','Natif','ネイティブ','네이티브','Natywne','Nativo','Нативный','原生']),
('builtin','Built-in capture',['Integrierte Erfassung','Captura integrada','Capture intégrée','内蔵キャプチャ','내장 캡처','Wbudowane przechwytywanie','Captura integrada','Встроенный захват','内置捕获']),
('reshade','ReShade',['ReShade']*9),
('external','External',['Extern','Externo','Externe','外部','외부','Zewnętrzne','Externo','Внешний','外部']),
('none','No current signal',['Kein aktuelles Signal','Sin señal actual','Aucun signal actuel','現在の信号なし','현재 신호 없음','Brak bieżącego sygnału','Sem sinal atual','Нет текущего сигнала','无当前信号']),
('used','In use',['Wird verwendet','En uso','En cours d’utilisation','使用中','사용 중','W użyciu','Em uso','Используется','正在使用']),
('detected','Detected, not in use',['Erkannt, nicht verwendet','Detectado, sin usar','Détecté, non utilisé','検出済み・未使用','감지됨, 사용하지 않음','Wykryto, nieużywane','Detectado, sem uso','Обнаружен, не используется','已检测到，未使用']),
('reason.ready','Ready',['Bereit','Listo','Prêt','準備完了','준비됨','Gotowe','Pronto','Готово','就绪']),
('reason.unavailable','Unavailable',['Nicht verfügbar','No disponible','Indisponible','利用不可','사용 불가','Niedostępne','Indisponível','Недоступно','不可用']),
('title','Input sources (experimental)',['Eingabequellen (experimentell)','Fuentes de entrada (experimental)','Sources d’entrée (expérimental)','入力ソース（試験機能）','입력 소스 (실험적)','Źródła danych wejściowych (eksperymentalne)','Fontes de entrada (experimental)','Источники входных данных (экспериментально)','输入来源（实验性）']),
('scope','Per-signal selection requires prepared D3D12 input. Other routes keep their current input set.',['Die Auswahl pro Signal erfordert aufbereitete D3D12-Eingaben. Andere Pfade behalten ihre aktuellen Eingaben.','La selección por señal requiere entradas D3D12 preparadas. Las demás rutas conservan sus entradas actuales.','La sélection par signal nécessite des entrées D3D12 préparées. Les autres chemins conservent leurs entrées actuelles.','信号ごとの選択には、準備済みのD3D12入力が必要です。他の経路では現在の入力セットを使用します。','신호별 선택에는 준비된 D3D12 입력이 필요합니다. 다른 경로는 현재 입력을 유지합니다.','Wybór dla poszczególnych sygnałów wymaga przygotowanych danych D3D12. Inne ścieżki zachowują obecne dane.','A seleção por sinal requer entradas D3D12 preparadas. As outras rotas mantêm as entradas atuais.','Выбор по сигналам требует подготовленных входных данных D3D12. Остальные пути сохраняют текущий набор данных.','按信号选择需要已准备好的 D3D12 输入。其他路径保留当前输入集。']),
('restore','Restore Auto',['Auf Automatisch zurücksetzen','Restaurar automático','Rétablir Auto','自動に戻す','자동으로 복원','Przywróć wybór automatyczny','Restaurar automático','Вернуть автовыбор','恢复自动']),
('strict','Strict input lock (experimental)',['Strikte Quellenbindung (experimentell)','Bloqueo estricto de fuentes (experimental)','Verrouillage strict des sources (expérimental)','入力ソースを厳密に固定（試験機能）','입력 소스 엄격 고정 (실험적)','Ścisła blokada źródeł (eksperymentalna)','Bloqueio estrito de fontes (experimental)','Строгая фиксация источников (экспериментально)','严格锁定输入来源（实验性）']),
('reason.identity','Frame identity unavailable',['Frame-Identität nicht verfügbar','Identidad del fotograma no disponible','Identité de l’image indisponible','フレーム識別情報なし','프레임 식별 정보 없음','Brak identyfikacji klatki','Identidade do quadro indisponível','Идентификатор кадра недоступен','帧标识不可用']),
('reason.frame','Wrong frame',['Falscher Frame','Fotograma incorrecto','Image incorrecte','フレーム不一致','프레임 불일치','Niewłaściwa klatka','Quadro incorreto','Другой кадр','帧不匹配']),
('reason.view','Wrong view',['Falsche Ansicht','Vista incorrecta','Vue incorrecte','ビュー不一致','뷰 불일치','Niewłaściwy widok','Vista incorreta','Другой вид','视图不匹配']),
('reason.target','Wrong target',['Falsches Ziel','Destino incorrecto','Cible incorrecte','対象不一致','대상 불일치','Niewłaściwy cel','Destino incorreto','Другая цель','目标不匹配']),
('reason.generation','Source changed',['Quelle geändert','La fuente ha cambiado','Source modifiée','ソースが変更されました','소스 변경됨','Źródło zostało zmienione','A fonte foi alterada','Источник изменился','来源已更改']),
('reason.device','Incompatible device',['Inkompatibles Gerät','Dispositivo incompatible','Périphérique incompatible','互換性のないデバイス','호환되지 않는 장치','Niezgodne urządzenie','Dispositivo incompatível','Несовместимое устройство','设备不兼容']),
('reason.stage','Unavailable before this stage',['Vor dieser Stufe nicht verfügbar','No disponible antes de esta etapa','Indisponible avant cette étape','この処理段階までに利用できません','이 단계 이전에는 사용 불가','Niedostępne przed tym etapem','Indisponível antes desta etapa','Недоступно до этого этапа','在此阶段之前不可用']),
('reason.convention','Unknown convention',['Unbekannte Konvention','Convención desconocida','Convention inconnue','データ規約不明','데이터 규약 알 수 없음','Nieznana konwencja','Convenção desconhecida','Неизвестное соглашение о данных','数据约定未知']),
('reason.interval','Wrong motion interval',['Falsches Bewegungsintervall','Intervalo de movimiento incorrecto','Intervalle de mouvement incorrect','モーションのフレーム区間不一致','모션 프레임 구간 불일치','Niewłaściwy przedział ruchu','Intervalo de movimento incorreto','Другой интервал движения','运动帧区间不匹配']),
('reason.raster','Unsupported image mapping',['Nicht unterstützte Bildzuordnung','Mapeo de imagen no compatible','Correspondance d’image non prise en charge','未対応の画像マッピング','지원되지 않는 이미지 매핑','Nieobsługiwane mapowanie obrazu','Mapeamento de imagem não compatível','Неподдерживаемое сопоставление изображения','不支持的图像映射']),
('reason.sync','Missing synchronization',['Synchronisierung fehlt','Falta sincronización','Synchronisation manquante','同期情報なし','동기화 정보 없음','Brak synchronizacji','Sincronização ausente','Нет синхронизации','缺少同步']),
('reason.lease','Resource ownership unavailable',['Ressourcenzugriff nicht gesichert','Uso seguro del recurso no garantizado','Accès sûr à la ressource non garanti','リソースの安全な使用を保証できません','리소스의 안전한 사용을 보장할 수 없음','Brak gwarancji bezpiecznego dostępu do zasobu','Uso seguro do recurso não garantido','Безопасный доступ к ресурсу не подтверждён','无法保证资源的安全使用']),
('reason.activation','Needs activation',['Aktivierung erforderlich','Requiere activación','Activation requise','有効化が必要','활성화 필요','Wymaga aktywacji','Requer ativação','Требуется активация','需要激活']),
('reason.fallback','Requested source unavailable; fallback active',['Gewünschte Quelle nicht verfügbar; Ersatzquelle aktiv','Fuente solicitada no disponible; alternativa activa','Source demandée indisponible ; repli actif','指定ソースを利用できないため、代替ソースを使用中','요청한 소스 사용 불가; 대체 소스 사용 중','Żądane źródło niedostępne; używane źródło zastępcze','Fonte solicitada indisponível; alternativa ativa','Запрошенный источник недоступен; используется резервный','请求的来源不可用；正在使用备用来源']),
('reason.provider','Unsupported provider',['Nicht unterstützter Anbieter','Proveedor no compatible','Fournisseur non pris en charge','未対応のプロバイダー','지원되지 않는 제공자','Nieobsługiwany dostawca','Provedor não compatível','Неподдерживаемый поставщик','不支持的提供程序'])]
english=json.loads((ROOT/'localization/english.json').read_text(encoding='utf-8'))
byid={x['id']:x for x in english}
for key,text,translations in rows:
    assert len(translations)==len(locales)
    id='ingame.input-sources.'+key
    byid[id]=dict(id=id,surface='in_game',section='input-sources',control='label',english=text,
        context='Experimental per-signal source selection at the prepared D3D12 acquisition boundary.',
        placeholders=[],preserve=['D3D12'] if 'D3D12' in text else [],line_breaks=False,revision=1)
english=sorted(byid.values(),key=lambda x:x['id'])
(ROOT/'localization/english.json').write_text(json.dumps(english,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
bundlepath=ROOT/'OptiScaler/menu/localization/BundledPacks.inc'
# Bundles are generated below from their authoritative per-locale files.
embedded=[json.loads((ROOT/f'localization/bundled/{locale}.nrlang').read_text(encoding='utf-8')) for locale in locales]
for index,locale in enumerate(locales):
    path=ROOT/f'localization/bundled/{locale}.nrlang';pack=json.loads(path.read_text(encoding='utf-8'))
    entries={x['id']:x for x in pack['in_game']}
    for key,text,translations in rows:
        entries['ingame.input-sources.'+key]=dict(id='ingame.input-sources.'+key,translation=translations[index],revision=1,note='Contextual experimental input-source translation.')
    pack['in_game']=sorted(entries.values(),key=lambda x:x['id'])
    path.write_text(json.dumps(pack,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    matched=[i for i,x in enumerate(embedded) if x['locale']==locale];assert len(matched)==1
    embedded[matched[0]]=pack
def emit(path,name,delimiter,data,comment):
    text=json.dumps(data,ensure_ascii=True,separators=(',',':'))
    lines=[comment,f'static const char* const {name}[] = {{']
    lines += [f'R"{delimiter}('+text[i:i+12000]+f'){delimiter}",' for i in range(0,len(text),12000)]
    path.write_text('\n'.join(lines+['};','']),encoding='utf-8')
lines=['// Generated from localization/bundled; indexed packs preserve lazy loading.']
for i,pack in enumerate(embedded):
    data=json.dumps(pack,ensure_ascii=True,separators=(',',':'))
    lines += [f'static const char* const EmbeddedPack{i}Chunks[] = {{']
    lines += ['R"NR_BUNDLE('+data[j:j+12000]+')NR_BUNDLE",' for j in range(0,len(data),12000)]+['};']
lines += ['static const EmbeddedPack EmbeddedPacks[] = {']
for i,pack in enumerate(embedded):
    lines += [f' {{"{pack["packId"]}","{pack["locale"]}",EmbeddedPack{i}Chunks,std::size(EmbeddedPack{i}Chunks)}},']
bundlepath.write_text('\n'.join(lines+['};','']),encoding='utf-8')
emit(ROOT/'OptiScaler/menu/localization/EnglishInventory.inc','EmbeddedEnglishChunks','NR_ENGLISH',english,'// Canonical inventory with experimental input-source additions.')
print(f'PASS appended {len(rows)} contextual strings in {len(locales)} locales; existing entries retained')
