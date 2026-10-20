"""Branch-local project and translation registration (idempotent)."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[2]
cap=root/'OptiScaler/nr/diagnostics/capability'
for name in ('OptiScaler.vcxproj','OptiScaler.vcxproj.filters'):
    path=root/'OptiScaler'/name
    text=path.read_text()
    additions=[]
    for source in sorted(cap.glob('*')):
        if source.suffix not in ('.cpp','.h'): continue
        include='nr\\diagnostics\\capability\\'+source.name
        if f'Include="{include}"' in text: continue
        kind='ClCompile' if source.suffix=='.cpp' else 'ClInclude'
        body='<PrecompiledHeader>NotUsing</PrecompiledHeader>' if kind=='ClCompile' and name.endswith('vcxproj') else ''
        additions.append(f'<{kind} Include="{include}">{body}</{kind}>')
    if additions: path.write_text(text.replace('</Project>','<ItemGroup>\n'+'\n'.join(additions)+'\n</ItemGroup>\n</Project>'))
rows=[
['Refresh observations','Actualizar observaciones','Actualiser les observations','Beobachtungen aktualisieren','Atualizar observações'],
['Copy observation report','Copiar informe de observaciones','Copier le rapport d’observations','Beobachtungsbericht kopieren','Copiar relatório de observações'],
['Observation report copied','Informe de observaciones copiado','Rapport d’observations copié','Beobachtungsbericht kopiert','Relatório de observações copiado'],
['Observation report unavailable','Informe de observaciones no disponible','Rapport d’observations indisponible','Beobachtungsbericht nicht verfügbar','Relatório de observações indisponível'],
['Settings observation: %s (at refresh)','Observación de ajustes: %s (al actualizar)','Observation des paramètres : %s (à l’actualisation)','Einstellungsbeobachtung: %s (bei Aktualisierung)','Observação de ajustes: %s (na atualização)'],
['current owner revision captured','revisión actual del propietario capturada','révision actuelle du propriétaire capturée','aktuelle Besitzerrevision erfasst','revisão atual do proprietário capturada'],
['unknown or changed during collection','desconocido o cambiado durante la recopilación','inconnu ou modifié pendant la collecte','unbekannt oder während der Erfassung geändert','desconhecido ou alterado durante a coleta'],
['Renderer observations are descriptive; input and image quality remain unqualified.','Las observaciones del renderizador son descriptivas; las entradas y la calidad de imagen siguen sin verificar.','Les observations du moteur de rendu sont descriptives ; les entrées et la qualité d’image restent non vérifiées.','Renderer-Beobachtungen sind beschreibend; Eingaben und Bildqualität bleiben ungeprüft.','As observações do renderizador são descritivas; as entradas e a qualidade de imagem continuam sem verificação.'],
['Observations unavailable','Observaciones no disponibles','Observations indisponibles','Beobachtungen nicht verfügbar','Observações indisponíveis']]
locale=root/'OptiScaler/menu/locales'
path=locale/'integration.json'
data=json.loads(path.read_text(encoding='utf-8'))
keys={r[0] for r in data}
data.extend(r for r in rows if r[0] not in keys)
path.write_text('[\n'+',\n'.join('  '+json.dumps(row,ensure_ascii=False) for row in data)+'\n]\n',encoding='utf-8')
for index,lang in enumerate(('es','fr','de','pt')):
    path=locale/(lang+'.json'); data=json.loads(path.read_text(encoding='utf-8'))
    data.update({r[0]:r[index+1] for r in rows})
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2,sort_keys=True)+'\n',encoding='utf-8')
