# SPDX-License-Identifier: MPL-2.0
# Build-only observation of five function entries. The production files stay
# untouched; fail closed if a definition moves or changes. No body is replaced.
BEGIN {
    print "#include \"tests/helpers/pipeline_counts.h\""
    fields["maelys_datalog_parse_only"] = "parses"
    fields["maelys_datalog_validate_program"] = "validations"
    fields["maelys_datalog_compute_program_fingerprint"] = "fingerprints"
    fields["maelys_datalog_prepared_session_create"] = "preparations"
    fields["maelys_datalog_prepared_session_materialize_inputs"] = "materializations"
}
/^maelys_result_t maelys_datalog_/ {
    name = $2
    sub(/\(.*/, "", name)
    if (name in fields) pending = fields[name]
}
{
    print
    if (pending != "" && /\{/) {
        print "    ++maelys_datalog_pipeline_counts." pending ";"
        pending = ""
        count++
    }
}
END {
    expected = FILENAME ~ /prepared_session[.]c$/ ? 2 : 1
    if (pending != "" || count != expected) {
        print "pipeline instrumentation: unexpected function definitions in " FILENAME > "/dev/stderr"
        exit 1
    }
}
