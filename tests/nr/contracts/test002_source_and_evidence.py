"""Corrected candidate static wiring and result-framing checks. NOT_RUN in Chat preparation.

This parses TEST evidence framing, never a C01/C02 fixture and never model/resource inputs.
Production FixtureCodec remains the only canonical fixture parser and C15 structural validator.
"""
from __future__ import annotations
import argparse
import json
import math
import re
import sys
from pathlib import Path

LIMIT = 16 * 1024 * 1024
ASSERTION_KEYS = {
    "kind", "suite", "run_id", "run_namespace", "record_namespace", "ordinal", "id", "fixture",
    "classification", "sut", "oracle", "expected", "actual", "expected_fnv1a64", "actual_fnv1a64",
    "execution", "matched"
}
C15_KEYS = {
    "kind", "suite", "run_id", "run_namespace", "record_namespace", "ordinal", "id", "fixture",
    "classification", "expected_fnv1a64", "actual_fnv1a64", "record"
}
OMITTED_KEYS = {
    "kind", "suite", "run_id", "run_namespace", "record_namespace", "ordinal", "id", "fixture",
    "classification", "expected_fnv1a64", "actual_fnv1a64", "reason"
}
SUMMARY_KEYS = {
    "kind", "suite", "run_id", "run_namespace", "record_namespace", "ordinal_count",
    "semantic_assertions", "preconditions", "mismatches", "unbound", "gate_approval", "exit_scope"
}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON member: " + key)
        result[key] = value
    return result


def bounded_integer(text):
    if len(text) > 21:
        raise ValueError("integer field length limit")
    return int(text)


def bounded_float(text):
    if len(text) > 64:
        raise ValueError("float field length limit")
    result = float(text)
    if not math.isfinite(result):
        raise ValueError("nonfinite float")
    return result


def load_json(text):
    return json.loads(text, object_pairs_hook=unique_object, parse_int=bounded_integer, parse_float=bounded_float,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError("nonfinite JSON: " + value)))


def bounded_text(path: Path, limit: int = LIMIT) -> str:
    if path.is_symlink() or not path.is_file():
        raise ValueError("regular non-symlink file required")
    with path.open("rb") as handle:
        data = handle.read(limit + 1)
    if len(data) > limit:
        raise ValueError("evidence/source size limit")
    return data.decode("utf-8-sig", errors="strict")


def check(condition, ident, expected, actual):
    if isinstance(actual, bool):
        actual = bool(condition)
    print(json.dumps({"kind": "static-assertion", "id": ident, "expected": expected,
                      "actual": actual, "matched": bool(condition),
                      "evidence_scope": "source-or-report-framing-only"}, ensure_ascii=True), flush=True)
    if not condition:
        raise AssertionError(ident)


def fnv1a64(text: str) -> str:
    value = 14695981039346656037
    for byte in text.encode("utf-8", errors="strict"):
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return str(value)


def exact_keys(obj: dict, expected: set[str], label: str):
    if not isinstance(obj, dict) or set(obj) != expected:
        raise ValueError(label + " exact-field mismatch")


def source_checks(root: Path):
    catalog = bounded_text(root / "tests/nr/oracles/Test002AnalyticCases.h")
    analytics = bounded_text(root / "tests/nr/oracles/nr_test002_analytics.cpp")
    identity = bounded_text(root / "tests/nr/contracts/nr_test002_identity.cpp")
    support = bounded_text(root / "tests/nr/contracts/Test002Support.h")
    verifier = bounded_text(root / "tests/nr/contracts/test002_source_and_evidence.py")
    compare = catalog.split("inline void Compare(", 1)[1]
    cases = catalog.split("inline std::vector<Case> Cases()", 1)[1].split("inline void Compare(", 1)[0]
    check("probe(c.input)" in compare and "probe(c.expected)" not in compare,
          "T11.static.expected-not-sut-input", "probe receives Input only", "probe(c.input)" in compare)
    check("const auto expectedText = !c.expected.known" in compare and "expectedText = actualText" not in compare,
          "T06.static.oracle-not-actual", "expected text comes from literal Expected", "expectedText = actualText" not in compare)
    check("probe(" not in cases and "Codec::" not in cases and "DlssNrWorkingMotionScale(" not in cases,
          "T11.static.literal-oracles", "catalog does not call production for expected values", True)
    check("constexpr Analytic::Probe productionProbe = nullptr;" in analytics,
          "T11.static.unbound-disclosed", "no invented production binding", True)
    check("F::CheckProjection(" not in identity and "F::ExpectedProjection(" not in identity,
          "T08.static.no-shared-projector", "no semantic expected projector reuse", True)
    check("enum class Verdict" not in support and "C::ValidationFixture f;" in support,
          "T06.static.canonical-verdict", "uses ARCH C15 without duplicate verdict enum", True)
    check("claim.source" not in support and "CreateCommandList(" not in support and "LoadLibrary" not in support,
          "T10.static.no-runtime-actions", "no selected prohibited runtime entry points", True)
    inventory = load_json(bounded_text(root / "tests/nr/contracts/nr_test002_inventory.json"))
    symbol_literals = []
    for path in (root / "tests/nr/contracts").glob("*.*"):
        if path.suffix not in {".h", ".cpp"}: continue
        symbol_literals.extend(re.findall(r'\bSym\("([^"]*)"\)', bounded_text(path)))
    symbol_literals.extend(re.findall(r'\bSym\("([^"]*)"\)', catalog))
    # Dynamic Symbol destinations are checked through their complete authored domains: case IDs,
    # emitted suites, classification names, C15 claim fields and missing-coverage names. The
    # runner-owned 32-hex ID and its composed namespaces are separately constrained below.
    symbol_literals.extend(row["id"] for rows in inventory["suites"].values() for row in rows)
    symbol_literals.extend(meta["emitted_suite"] for meta in inventory["suite_metadata"].values())
    symbol_literals.extend(("CURRENT_CALLABLE", "LATER_OWNER_BINDING", "FIXTURE_PRECONDITION"))
    symbol_literals.extend(re.findall(r'pushClaim\("([^"]+)"', support))
    symbol_literals.extend(re.findall(r'AddGap\([^\n]*?"([^"]+)"\)', support))
    legal = re.compile(r'^[A-Za-z0-9_.:-]+$')
    check(bool(symbol_literals) and all(legal.fullmatch(value) for value in symbol_literals),
          "T06.static.symbol-grammar", "all accepted literal/dynamic Symbol domains use canonical grammar", True)
    correlation_tokens = ("run_id", "run_namespace", "record_namespace", "expected_fnv1a64",
                          "actual_fnv1a64", "FixtureIdentity", "ProductionBindingUnavailable")
    check(all(token in support for token in correlation_tokens) and "--run-id" in verifier,
          "T06.static.case-run-correlation", "raw/C15 evidence binds case, suite and run", True)
    negative_ids = ("T09.signature.selected-route-different", "T09.signature.selected-resource-different",
                    "T09.signature.selected-representation-different", "T09.signature.selected-record-different")
    check(all(ident in identity for ident in negative_ids),
          "T09.static.signature-negative-controls", "valid unequal selected signatures are explicit", True)
    print(json.dumps({"kind": "static-summary", "checks": 10,
                      "limitations": "lexical guard only; independent semantic review still required",
                      "gate_approval": "NOT_PERFORMED"}), flush=True)


def metadata_body(envelope: dict, reference: dict, label: str):
    if not isinstance(reference, dict):
        raise ValueError(label + " metadata reference missing")
    matches = [entry for entry in envelope.get("metadata", [])
               if isinstance(entry, dict) and entry.get("reference") == reference]
    if len(matches) != 1 or set(matches[0]) != {"reference", "value"}:
        raise ValueError(label + " metadata reference not uniquely resolved")
    return matches[0]["value"]


def list_body(envelope: dict, descriptor: dict, label: str):
    if not isinstance(descriptor, dict) or set(descriptor) != {"count", "backing"}:
        raise ValueError(label + " metadata-list shape")
    count = descriptor["count"]
    if not isinstance(count, int) or count < 0:
        raise ValueError(label + " metadata-list count")
    if count == 0:
        if descriptor["backing"] is not None:
            raise ValueError(label + " empty metadata-list has backing")
        return []
    body = metadata_body(envelope, descriptor["backing"], label)
    if not isinstance(body, list) or len(body) != count:
        raise ValueError(label + " metadata-list body/count mismatch")
    return body


def scalar_fact(fact: dict, expected_type: str, label: str):
    if not isinstance(fact, dict) or set(fact) != {"known", "value", "evidence"} or fact["known"] is not True:
        raise ValueError(label + " must be a Known scalar fact")
    value = fact["value"]
    if not isinstance(value, dict) or set(value) != {"type", "value"} or value["type"] != expected_type:
        raise ValueError(label + " scalar variant mismatch")
    return value["value"]


def claims_by_field(envelope: dict, descriptor: dict):
    claims = list_body(envelope, descriptor, "environment")
    result = {}
    for claim in claims:
        if not isinstance(claim, dict) or "field" not in claim or claim["field"] in result:
            raise ValueError("invalid or duplicate environment claim")
        result[claim["field"]] = claim
    return result


def gap_values(envelope: dict, axis: dict, label: str):
    return list_body(envelope, axis["missingCoverage"], label + ".missingCoverage")


def verify_outer(obj: dict, raw: dict, expected_keys: set[str], label: str):
    exact_keys(obj, expected_keys, label)
    for key in ("suite", "run_id", "run_namespace", "record_namespace", "ordinal", "id", "fixture",
                "classification", "expected_fnv1a64", "actual_fnv1a64"):
        if obj[key] != raw[key]:
            raise ValueError(label + " transplanted/mismatched " + key)


def verify_log(inventory: dict, suite: str, path: Path, run_dir: Path, run_id: str, raw_only: bool):
    if path.resolve().parent != run_dir.resolve():
        raise ValueError("log must be an immediate child of the owned evidence directory")
    if not re.fullmatch(r"[0-9a-f]{32}", run_id):
        raise ValueError("runner-owned run ID must be 32 lowercase hex characters")
    if suite not in inventory["suites"] or suite not in inventory["suite_metadata"]:
        raise ValueError("unknown registry suite")
    emitted_suite = inventory["suite_metadata"][suite]["emitted_suite"]
    run_namespace = "test002." + run_id
    record_namespace = run_namespace + "." + emitted_suite
    rows = inventory["suites"][suite]
    expected = {row["id"]: row for row in rows}
    if len(expected) != len(rows):
        raise ValueError("duplicate inventory case ID")
    seen, canonical, omitted, summaries = {}, {}, {}, []
    line_kinds = []
    text = bounded_text(path)
    lines = text.splitlines()
    if len(lines) > 8192:
        raise ValueError("record count limit")
    for line_number, line in enumerate(lines, 1):
        if len(line) > 1024 * 1024:
            raise ValueError("record byte limit")
        obj = load_json(line)
        kind = obj.get("kind")
        line_kinds.append(kind)
        if kind == "suite-summary":
            exact_keys(obj, SUMMARY_KEYS, "suite summary")
            summaries.append((line_number, obj))
            continue
        ident = obj.get("id")
        if ident not in expected:
            raise ValueError("unknown assertion ID")
        if kind == "assertion":
            exact_keys(obj, ASSERTION_KEYS, "raw assertion")
            target = seen
        elif kind == "c15":
            exact_keys(obj, C15_KEYS, "C15 wrapper")
            target = canonical
        elif kind == "c15-omitted":
            exact_keys(obj, OMITTED_KEYS, "C15 omission")
            target = omitted
        else:
            raise ValueError("unknown evidence record kind")
        if ident in target:
            raise ValueError("duplicate evidence ID")
        target[ident] = obj
    if set(seen) != set(expected) or len(summaries) != 1:
        raise ValueError("missing assertion/terminal summary")
    if summaries[0][0] != len(lines) or line_kinds[-1] != "suite-summary":
        raise ValueError("terminal summary is not the final record")

    current = preconditions = pending = 0
    ordinals = set()
    for ident, row in expected.items():
        raw = seen[ident]
        exact_keys(raw, ASSERTION_KEYS, "raw assertion")
        binding = row["binding"]
        if binding not in {"CURRENT_CALLABLE", "LATER_OWNER_BINDING", "FIXTURE_PRECONDITION"}:
            raise ValueError("unknown binding classification")
        expected_execution = "NOT_RUN" if binding == "LATER_OWNER_BINDING" else "COMPLETED"
        if raw["suite"] != emitted_suite or raw["run_id"] != run_id or raw["run_namespace"] != run_namespace or \
           raw["record_namespace"] != record_namespace or raw["fixture"] != ident or raw["classification"] != binding:
            raise ValueError("raw case/suite/run correlation mismatch")
        if raw["execution"] != expected_execution:
            raise ValueError("execution differs from registered binding state")
        if not isinstance(raw["ordinal"], str) or not raw["ordinal"].isdigit() or raw["ordinal"] in ordinals:
            raise ValueError("invalid or duplicate assertion ordinal")
        ordinals.add(raw["ordinal"])
        if raw["expected_fnv1a64"] != fnv1a64(raw["expected"]) or raw["actual_fnv1a64"] != fnv1a64(raw["actual"]):
            raise ValueError("raw expected/actual digest mismatch")
        if binding == "LATER_OWNER_BINDING":
            pending += 1
            if raw["matched"] is not None:
                raise ValueError("later-owner case acquired result")
        else:
            if not isinstance(raw["matched"], bool):
                raise ValueError("completed raw record has no Boolean result")
            if binding == "FIXTURE_PRECONDITION": preconditions += 1
            else: current += 1

        if raw_only:
            if ident not in omitted or canonical:
                raise ValueError("mutation reporting must explicitly omit C15")
            omission = omitted[ident]
            verify_outer(omission, raw, OMITTED_KEYS, "C15 omission")
            if omission["reason"] != "MUTATION_RAW_ONLY":
                raise ValueError("wrong C15 omission reason")
            continue
        if ident not in canonical or omitted:
            raise ValueError("normal mode missing canonical evidence")
        wrapper = canonical[ident]
        verify_outer(wrapper, raw, C15_KEYS, "C15 wrapper")
        envelope = wrapper["record"]
        if not isinstance(envelope, dict) or envelope.get("schema") != "nr.arch.fixture" or \
           envelope.get("major") != 2 or envelope.get("type") != "ValidationFixture":
            raise ValueError("wrong C15 envelope")
        record = envelope.get("record")
        header = record.get("header") if isinstance(record, dict) else None
        if not isinstance(header, dict) or header.get("contract") != "C15" or header.get("owner") != "Validation":
            raise ValueError("wrong embedded C15 header")
        key = header.get("record")
        scope = header.get("scope")
        if key != {"nameSpace": record_namespace, "issuer": "test002.owner", "value": raw["ordinal"]}:
            raise ValueError("embedded C15 record identity mismatch")
        if scope != {"known": True, "identity": {"nameSpace": run_namespace, "issuer": "test002.owner", "value": "1"}}:
            raise ValueError("embedded C15 run scope mismatch")
        invariants = list_body(envelope, record["expectedInvariants"], "expectedInvariants")
        if invariants != [ident]:
            raise ValueError("embedded invariant/case mismatch")
        claims = claims_by_field(envelope, record["environment"])
        required_claims = {"SyntheticNoGpuPayload", "RunIdentity", "SuiteIdentity", "FixtureIdentity",
                           "RawExpectedFnv1a64", "RawActualFnv1a64", "EvidenceClassification"}
        if set(claims) != required_claims:
            raise ValueError("C15 environment correlation claim set mismatch")
        for field, expected_value, expected_type in (
            ("RunIdentity", run_id, "Symbol"), ("SuiteIdentity", emitted_suite, "Symbol"),
            ("FixtureIdentity", ident, "Symbol"), ("EvidenceClassification", binding, "Symbol"),
            ("RawExpectedFnv1a64", raw["expected_fnv1a64"], "UInt64"),
            ("RawActualFnv1a64", raw["actual_fnv1a64"], "UInt64")):
            claim = claims[field]
            if scalar_fact(claim["raw"], expected_type, field + ".raw") != expected_value or \
               scalar_fact(claim["effective"], expected_type, field + ".effective") != expected_value:
                raise ValueError("C15 environment correlation value mismatch: " + field)
        no_gpu = claims["SyntheticNoGpuPayload"]
        if scalar_fact(no_gpu["raw"], "Bool", "SyntheticNoGpuPayload.raw") is not True or \
           scalar_fact(no_gpu["effective"], "Bool", "SyntheticNoGpuPayload.effective") is not True:
            raise ValueError("synthetic CPU-only claim mismatch")

        axes = record["result"]
        protocol = axes["protocolSemantic"]
        if binding == "CURRENT_CALLABLE":
            if protocol["execution"] != "COMPLETED":
                raise ValueError("current-callable C15 execution mismatch")
            verdict = protocol["verdict"]
            if not verdict["known"] or verdict["value"] != ("PASS" if raw["matched"] else "FAIL"):
                raise ValueError("C15 verdict differs from raw assertion")
        else:
            if protocol["execution"] != "NOT_RUN" or protocol["verdict"]["known"]:
                raise ValueError("non-semantic case promoted into a C15 verdict")
            required_gap = "ProductionBindingUnavailable" if binding == "LATER_OWNER_BINDING" else "FixturePreconditionOnly"
            if required_gap not in gap_values(envelope, protocol, "protocolSemantic"):
                raise ValueError("pending/precondition protocol coverage gap missing")
        for name in ("gpuResource", "temporalImageQuality", "performanceLatency"):
            axis = axes[name]
            if axis["execution"] != "NOT_RUN" or axis["verdict"]["known"] or not gap_values(envelope, axis, name):
                raise ValueError("unexecuted axis promoted or coverage missing")

    terminal = summaries[0][1]
    if terminal["gate_approval"] != "NOT_PERFORMED" or terminal["exit_scope"] != "executed-subset-only":
        raise ValueError("terminal summary promoted gate approval or coverage scope")
    if terminal["suite"] != emitted_suite or terminal["run_id"] != run_id or \
       terminal["run_namespace"] != run_namespace or terminal["record_namespace"] != record_namespace:
        raise ValueError("terminal summary case/suite/run mismatch")
    mismatches = sum(row["matched"] is False for row in seen.values())
    if terminal["ordinal_count"] != str(len(expected)) or terminal["semantic_assertions"] != current or \
       terminal["preconditions"] != preconditions or terminal["unbound"] != pending or \
       terminal["mismatches"] != mismatches:
        raise ValueError("summary counts disagree with raw records")
    print(json.dumps({"kind": "evidence-framing", "suite": suite, "emitted_suite": emitted_suite,
                      "run_id": run_id, "expected_records": len(expected), "observed_records": len(seen),
                      "current_callable": current, "preconditions": preconditions, "unbound": pending,
                      "mismatches": mismatches, "raw_only": raw_only,
                      "canonical_decode_scope": "C++ production codec, not this Python framing check",
                      "gate_approval": "NOT_PERFORMED"}), flush=True)
    # A well-framed failed assertion is not an evidence-infrastructure failure. Mutation adjudication
    # separately requires a successful control, mutant startup, the named mismatch, and exact run/case correlation.


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-check", action="store_true")
    parser.add_argument("--verify-log", type=Path)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--suite")
    parser.add_argument("--run-id")
    parser.add_argument("--mutation-raw-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    if not (root / "OptiScaler.sln").is_file():
        raise ValueError("wrong source root")
    if args.source_check:
        if args.verify_log or args.run_id:
            raise ValueError("choose exactly one mode")
        source_checks(root)
    elif args.verify_log and args.run_dir and args.suite and args.run_id:
        inventory = load_json(bounded_text(root / "tests/nr/contracts/nr_test002_inventory.json"))
        verify_log(inventory, args.suite, args.verify_log, args.run_dir, args.run_id, args.mutation_raw_only)
    else:
        raise ValueError("incomplete verification arguments")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(json.dumps({"kind": "infrastructure-error", "error": str(error),
                          "gate_approval": "NOT_PERFORMED"}), file=sys.stderr, flush=True)
        raise SystemExit(2)
