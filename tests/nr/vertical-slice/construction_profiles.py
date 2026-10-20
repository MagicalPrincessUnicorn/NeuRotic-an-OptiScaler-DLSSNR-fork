"""Code-owned compiler profiles for original Spectre construction oracles.

Registration permits execution, never an original-obligation PASS. The evaluator
separately requires reviewed source bytes and complete assertion groups.
"""
from __future__ import annotations
import json
import pathlib

KERNEL = ('OptiScaler/nr/orchestration/StreamCoordinatorKernel.cpp',
          'OptiScaler/nr/lifecycle/NativeSessionLifetime.cpp',
          'OptiScaler/nr/protocol/NativeRouteAdmission.cpp')

PROFILES = {
    'native-adversarial-authority': dict(
        sources=('tests/nr/orchestration/native_adversarial_authority.cpp', *KERNEL),
        inventory='tests/nr/orchestration/native_adversarial_inventory.json',
        defines=('NR_ORCH_INIT_TESTING',), libraries=()),
    'spectre-return-history': dict(
        sources=('tests/nr/spectre-obligations/return_history.cpp', *KERNEL),
        inventory='tests/nr/spectre-obligations/return_history_inventory.json',
        defines=('NR_ORCH_INIT_TESTING',), libraries=()),
    'spectre-final-boundary': dict(
        sources=('tests/nr/spectre-obligations/final_boundary.cpp',),
        inventory='tests/nr/spectre-obligations/final_boundary_inventory.json',
        defines=(), libraries=()),
    'consumption-leases': dict(
        sources=('tests/nr/lifecycle/consumption_tests.cpp',),
        inventory='tests/nr/lifecycle/consumption_inventory.json',
        defines=(), libraries=()),
    'finalizer-exactly-once': dict(
        sources=('tests/nr/finalizer/t27_exactly_once.cpp',),
        inventory='tests/nr/finalizer/finalizer_inventory.json',
        defines=(), libraries=()),
    'native-lifecycle': dict(
        sources=('tests/nr/vertical-slice/native_lifecycle_integration.cpp', *KERNEL),
        inventory='tests/nr/vertical-slice/native_lifecycle_inventory.json',
        defines=(), libraries=()),
    'native-same-recording': dict(
        sources=('tests/nr/vertical-slice/native_same_recording.cpp',),
        inventory='tests/nr/vertical-slice/native_same_recording_inventory.json', defines=(), libraries=()),
}
COMPOSITES = {'finalizer-duplicate-composite': ('finalizer-exactly-once', 'native-lifecycle')}
COMPILE_PROFILE = 'native-compile-authority'
COMPILE_SOURCE = 'tests/nr/orchestration/initial_native_authority.cpp'
# The intended MSVC diagnostic must point at this source statement, not an
# unrelated include, infrastructure failure, warning or missing linker input.
COMPILE_CASES = {
    1: ('raw-plan-cannot-construct-current-view', 'C2665', 'CurrentRouteView',
        'O::CurrentRouteView forged(plan);'),
    4: ('admission-capability-noncopyable', 'C2280', 'NativeInvocationAdmission',
        'auto copied=admission;'),
    5: ('raw-recipe-cannot-execute', 'C2672', 'ExecuteNativeProtocol',
        'P::ExecuteNativeProtocol(product,request,header,finalizer,owner,store,events,executor);'),
}
COMPILE_ASSERTIONS = ('legitimate-api-controls-compile', *(row[0] for row in COMPILE_CASES.values()))
MUTATIONS = {}  # profile -> name -> {changes: ((path,before,after),...), detectors: (row_id,...)}
_adversarial = json.loads((pathlib.Path(__file__).resolve().parents[1] /
    'orchestration/native_adversarial_mutations.json').read_text(encoding='utf-8-sig'))
if _adversarial['schema'] != 'NativeAdversarialSemanticMutants/1' or _adversarial['profile'] != 'native-adversarial-authority':
    raise ValueError('unexpected registered adversarial mutation schema')
MUTATIONS['native-adversarial-authority'] = _adversarial['mutations']
_additional = json.loads((pathlib.Path(__file__).resolve().parents[1] /
    'spectre-obligations/construction_mutations.json').read_text(encoding='utf-8-sig'))
if _additional['schema'] != 'SpectreObligationSemanticMutants/1':
    raise ValueError('unexpected obligation mutation schema')
MUTATIONS.update(_additional['profiles'])

CONSTRUCTION_BINDINGS = {
    'N12:compile-time:SOURCE_ANALYSIS': dict(suite=COMPILE_PROFILE,
        test_binding='N12.compile-time.exact-api-diagnostics/v1', assertion_ids=COMPILE_ASSERTIONS,
        scope='Production capability API compile rejection with three exact diagnostic oracles and legitimate API positive controls; no runtime or structural cell inferred.'),
    'L05:unit:CPU': dict(suite='consumption-leases', test_binding='L05.unit.provider-retention-predicate/v1',
        assertion_ids=('T17.first-completion', 'T16.fully-retired', 'T19.active-provider',
            'T19.unknown-release', 'T19.release-token-missing', 'T19.explicit-release', 'T19.wrong-target-release'),
        scope='Actual canonical C03 retention predicate with synthetic exterior owner dependency/release facts; GPU completion does not replace exact provider release. No actual provider or GPU execution.'),
    'F09:unit:CPU': dict(suite='finalizer-duplicate-composite', test_binding='F09.unit.immutable-seal-and-no-second-call/v1',
        assertion_ids=tuple('finalizer-exactly-once/' + ident for ident in
            ('T27.primary', 'T27.duplicate-claim', 'T27.produced', 'T27.prepare', 'T27.accept-one', 'T27.duplicate-seal')) +
            tuple('native-lifecycle/' + ident for ident in
            ('T30.native-integrated-produced', 'T30.native-integrated-three-rechecks', 'T27.native-integrated-no-second-call')),
        scope='Two independently executed C15 suites cover canonical immutable finalizer response plus no second executor call, with synthetic exterior owner fixtures; no actual Native strict association, provider, GPU or game claim.'),
}

for _case, _layer, _suite, _rows, _scope in (
    ('O06', 'unit', 'spectre-return-history',
     ('exact-content-control', 'wrong-allocation', 'wrong-generation', 'stale-content',
      'wrong-subresource', 'wrong-region', 'overflow-region', 'wrong-recording', 'substituted-owner'),
     'Production exact-output identity/content/range predicate, synthetic Resource metadata; no actual HostReturn publication or owned GPU revision claim.'),
    ('O07', 'state-machine', 'spectre-return-history',
     ('early-before-call', 'early-after-call', 'throwing-call', 'normal-explicit-restore', 'failed-restore-blocks-continuation'),
     'Actual parameter override restoration and sticky continuation state with controlled synthetic callback; no GPU rollback or NGX integration claim.'),
    ('F06', 'unit', 'spectre-final-boundary',
     ('canonical-association', 'wrong-view', 'wrong-base', 'wrong-stream', 'wrong-provider', 'wrong-handoff'),
     'Actual canonical sole finalizer exact association checks with synthetic exterior reservation, Resource and presentation metadata; no installed Native strict bridge.'),
    ('F12', 'unit', 'spectre-final-boundary',
     ('final-packet-retains-provider-hold', 'terminal-distinct-scratch', 'final-held-after-scratch', 'final-release-control'),
     'Canonical sole-finalizer packet and two independent C03 reuse predicates with synthetic Resource/provider facts; all other final retirement gates are terminal before exact unresolved hold refusal and release control. No actual Native strict bridge or physical provider release.'),
):
    CONSTRUCTION_BINDINGS[_case + ':' + _layer + ':CPU'] = dict(suite=_suite,
        test_binding=_case + '.' + _layer + '.canonical-owner-oracle/v1',
        assertion_ids=tuple(_case + '.' + row for row in _rows), scope=_scope)

_NATIVE_GROUPS = {
    'N03': ('unit', ('copied_plan_pre_revoke_start', 'copied_plan_stays_structural_after_revoke', 'copied_plan_post_revoke_no_authority')),
    'N05': ('state-machine', ('selection_frozen_before', 'selection_frozen_after')),
    'N06': ('unit', ('rekeyed_source_first_call', 'rekeyed_evaluation_no_opportunity', 'rekeyed_route_label_no_opportunity', 'rekeyed_sample_retirement_preserved', 'genuinely_new_sample_after_closure')),
    'N09': ('unit', ('native_ordinal_cannot_match_real_association', 'unknown_final_association_stays_unknown')),
    'N11': ('unit', ('same_sample_exact_recipe_resources', 'cross_sample_recipe_refused')),
    'N12': ('structural', ('raw_values_convey_no_authority', 'same_sample_exact_recipe_resources')),
    'O09': ('unit', ('failed_attempt_same_sample_not_retry', 'failed_history_requires_next_reset', 'new_sample_reset_requested_not_ack')),
    'O10': ('unit', ('optional_diagnostics_control', 'optional_diagnostics_failure_independent')),
    'O11': ('unit', ('full_table_no_preparation_or_effect', 'reserved_failure_preserves_older_slots')),
    'O12': ('unit', ('post_call_publication_refusal_retained', 'publication_failure_owner_until_terminal')),
    'L13': ('unit', ('late_ticket_after_reuse_refused', 'new_ticket_and_closed_prefix')),
    'L14': ('state-machine', ('unrelated_final_client_progress_after_native_stop', 'shared_finalizer_stops_after_all_clients')),
    'L15': ('unit', ('terminal_gpu_does_not_end_callback_reader', 'cold_root_waits_exact_cpu_tails')),
}
for _case, (_layer, _rows) in _NATIVE_GROUPS.items():
    _axis = 'SOURCE_ANALYSIS' if _layer == 'structural' else 'CPU'
    CONSTRUCTION_BINDINGS[_case + ':' + _layer + ':' + _axis] = dict(suite='native-adversarial-authority',
        test_binding=_case + '.' + _layer + '.canonical-authority/v1', assertion_ids=_rows,
        scope='Actual canonical coordinator/ledger/profile/finalizer methods and complete named comparison group with synthetic exterior owner facts. N05 uses a synthetic SR shim. No integration, model, proprietary provider, GPU or actual Native final association claim.')

for _suite, _mutants in MUTATIONS.items():
    for _name, _definition in _mutants.items():
        _case = _definition['case']
        if _case in ('O07', 'F07'):
            continue  # Meaningful hardening controls; no such original mutation cell.
        _key = _case + ':mutation:CPU'
        _control = _case + ':unit:CPU'
        if _control not in CONSTRUCTION_BINDINGS:
            continue
        _base = CONSTRUCTION_BINDINGS[_control]
        CONSTRUCTION_BINDINGS[_key] = dict(_base, mutant=True,
            test_binding=_case + '.mutation.canonical-authority/v1',
            mutation_names=tuple(name for name, definition in _mutants.items() if definition['case'] == _case),
            control_obligation_id=_control)


def members(profile):
    if profile == COMPILE_PROFILE:
        return ()
    if profile in COMPOSITES:
        return COMPOSITES[profile]
    if profile not in PROFILES:
        raise ValueError('unregistered construction profile')
    return (profile,)


def roots(profile):
    if profile == COMPILE_PROFILE:
        return (COMPILE_SOURCE,)
    return tuple(dict.fromkeys(path for name in members(profile) for path in PROFILES[name]['sources']))
