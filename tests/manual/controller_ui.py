"""Build test-only shared/split Console packs for the explicit graphical smoke.

Run from the repository root after building native. See docs/controller-content.md.
No production craft, tuning or assets are used.
"""

import json
import zipfile
from pathlib import Path
for split in (False, True):
    d = json.loads(Path('tests/fixtures/content/definitions.json').read_text())
    d['schema'] = 2
    d['entities'] = d['entities'][:1]
    e = d['entities'][0]
    e['id'] = 'test.controllable'
    e.pop('console')
    e['motion_model'] = 'vector'
    e['systems'] = {'port': {'kind': 'engine', 'max_speed_mm_s': 17000, 'acceleration_mm_s2': 2000}, 'starboard': {'kind': 'engine', 'max_speed_mm_s': 17000, 'acceleration_mm_s2': 1000}, 'steering': {'kind': 'steering', 'turn_mdeg_s': 90000}}
    e['stations'] = [{'id': 'operations', 'consoles': [{'id': 'drive', 'definition': 'test.console', 'systems': [], 'feeds': ['port', 'starboard', 'steering']}]}]
    owners = []
    widgets = []
    for i, (name, consumers) in enumerate([('port_drive', ['port']), ('starboard_drive', ['starboard'])] if split else [('propulsion', ['port', 'starboard'])]):
        owners.append({'id': name, 'console': 'drive', 'fallback': 'hold', 'commands': [{'id': 'speed', 'action': 'speed_mm_s', 'initial': 0, 'consumers': consumers}]})
        widgets.append({'id': name, 'kind': 'throttle', 'label': name + ' throttle', 'controller': name, 'channel': 'speed', 'x': i * 180, 'y': 0, 'width': 180, 'height': 1000})
    owners[0]['commands'].append({'id': 'heading', 'action': 'heading_mdeg', 'consumers': ['steering']})
    widgets.append({'id': 'course', 'kind': 'heading', 'label': 'Course', 'controller': owners[0]['id'], 'channel': 'heading', 'x': 360, 'y': 200, 'width': 200, 'height': 500})
    names = [o['id'] for o in owners]
    e['control_layout'] = {'schema': 1, 'default_profile': 'direct', 'controllers': owners, 'profiles': [{'id': 'direct', 'consoles': {'drive': {'human': names}}}, {'id': 'automatic', 'consoles': {'drive': {'ai': names}}}]}
    d['schema'] = 4
    d['knowledge'] = {'roles': ['crew'], 'schemas': [{
        'id': 'engine_report', 'producers': ['crew'], 'readers': ['crew'],
        'ship_publishable': True,
        'fields': [{'id': 'amount', 'type': 'integer', 'publishable': True}]
    }]}
    e['stations'][0]['consoles'][0]['knowledge_roles'] = ['crew']
    e['message_templates'] = ['test.engine_console', 'test.engine_public']
    d['messages'] = []
    e['systems']['port']['reporters'] = []
    for public in (False, True):
        key = 'test.engine_public' if public else 'test.engine_console'
        d['messages'].append({
            'id': key, 'schema': 'engine_report',
            'title_key': 'Engine public report' if public else 'Engine Console report',
            'body_key': 'Port engine requested speed: {amount} mm/s.',
            'destination': {'kind': 'ship'} if public else {
                'kind': 'controller', 'console': 'drive', 'id': names[0]},
            'urgency': 2 if public else 0, 'delay_ticks': 1,
            'expires_after_ticks': 600,
            'fields': {'amount': {'argument': 'amount'}},
            'parameters': {'amount': {'argument': 'amount'}}, 'automatic': False
        })
        e['systems']['port']['reporters'].append({
            'id': key, 'field': 'requested', 'above': 4000, 'message': key,
            'debounce_ticks': 2, 'minimum_gap_ticks': 5
        })
    d['consoles'][0]['widgets'] = widgets
    d['consoles'][0]['gameplay_panel'] = {'x': 0.57, 'y': 0, 'width': 0.38, 'height': 0.9}
    d['maps'][0]['entities'] = []
    d['maps'][0]['ship_slots'] = [{'id': 'test.slot', 'x_mm': 0, 'y_mm': 0, 'heading_mdeg': 0, 'allowed_ships': ['test.controllable']}]
    with zipfile.ZipFile('build/controller-ui-' + ('split' if split else 'shared') + '.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json', json.dumps({'schema': 1, 'id': 'test-core'}))
        z.writestr('definitions.json', json.dumps(d))
        z.write('tests/fixtures/content/pixel.png', 'assets/pixel.png')
