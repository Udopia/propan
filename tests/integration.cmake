# Probing integration tests: all counters depend only on propagation fixpoints and
# must be identical for every propagator. Expected values were produced by the
# OccurrencePropagator (reference implementation).
set(INSTANCES ${CMAKE_CURRENT_SOURCE_DIR}/data/instances)

function(add_probe_tests name file expected)
    foreach(prop occ watch)
        add_test(NAME probe.${name}.${prop} COMMAND probe ${INSTANCES}/${file} -p ${prop})
        set_tests_properties(probe.${name}.${prop} PROPERTIES
            PASS_REGULAR_EXPRESSION "${expected}" TIMEOUT 120 LABELS integration)
    endforeach()
endfunction()

add_probe_tests(1_TC_256_K_61_sanitized 1-TC-256-K-61.sanitized.cnf.xz
    "c probes 24738 failed 44 lifted 15 implied 2395607.*c root\-assigned 1951.*s UNKNOWN")
add_probe_tests(289_sat_14x8 289-sat-14x8.cnf.xz
    "c probes 895 failed 1 lifted 0 implied 1340.*c root\-assigned 1.*s UNKNOWN")
add_probe_tests(b14_s_PathRelaxation_FS_29 b14-s_PathRelaxation_FS_29.wcnf.xz
    "c probes 10482 failed 7 lifted 77 implied 394103.*c soft\-true 0 lower\-bound 45.*c block conflicting implied 0.*c greedy\-skipped 107 weight 107.*s UNKNOWN")
add_probe_tests(Break_triple_10_24_xml Break_triple_10_24.xml.cnf.xz
    "c probes 13523 failed 1 lifted 0 implied 266882.*c root\-assigned 2.*s UNKNOWN")
add_probe_tests(crn_11_100_s crn_11_100_s.cnf.xz
    "c probes 2600 failed 55 lifted 0 implied 889.*c root\-assigned 55.*s UNKNOWN")
add_probe_tests(data_405 data.405.wcnf.xz
    "c probes 1263 failed 0 lifted 0 implied 357.*c soft\-true 0 lower\-bound 0.*c block conflicting implied 0.*c greedy\-skipped 357 weight 357.*s UNKNOWN")
add_probe_tests(dlx2_aa dlx2_aa.cnf.xz
    "c probes 955 failed 13 lifted 2 implied 10758.*c root\-assigned 20.*s UNKNOWN")
add_probe_tests(driverlog04ac_wcsp driverlog04ac.wcsp.wcnf.xz
    "c probes 4760 failed 39 lifted 14 implied 190153.*c soft\-true 37 lower\-bound 0.*c block conflicting implied 0.*c greedy\-skipped 18 weight 1635.*s UNKNOWN")
add_probe_tests(engine_5_nd engine_5_nd.cnf.xz
    "c probes 34484 failed 806 lifted 278 implied 8728176.*c root\-assigned 2039.*s UNKNOWN")
add_probe_tests(ferry7_ks99i_renamed_as_sat05_4001 ferry7_ks99i.renamed-as.sat05-4001.cnf.xz
    "c probes 3818 failed 14 lifted 0 implied 108983.*c root\-assigned 44.*s UNKNOWN")
add_probe_tests(gripper10_used_as_sat04_419 gripper10.used-as.sat04-419.cnf.xz
    "c probes 4446 failed 0 lifted 50 implied 100349.*c root\-assigned 215.*s UNKNOWN")
add_probe_tests(ktf_TF_1_tf_3_0_04_68 ktf_TF-1.tf_3_0.04_68.cnf.xz
    "c probes 96014 failed 433 lifted 7984 implied 22994126.*c root\-assigned 9219.*s UNKNOWN")
add_probe_tests(lockchart_group2_rnd0_3_L18_K36_P8D4J1 lockchart-group2-rnd0.3-L18-K36-P8D4J1.cnf.xz
    "c probes 44928 failed 0 lifted 0 implied 4785192.*c root\-assigned 0.*s UNKNOWN")
add_probe_tests(logic_synthesis_normalized_apex4_a_opb_m logic-synthesis-normalized-apex4.a.opb.msat.wcnf.xz
    "c probes 15446 failed 7 lifted 0 implied 2242.*c soft\-true 0 lower\-bound 199.*c block conflicting implied 0.*c greedy\-skipped 799 weight 799.*s UNKNOWN")
add_probe_tests(max_clq_150_18_8493_4_clq max_clq_150-18-8493-4.clq.wcnf.xz
    "c probes 464 failed 0 lifted 0 implied 10864.*c soft\-true 0 lower\-bound 0.*c block conflicting implied 0.*c greedy\-skipped 136 weight 136.*s UNKNOWN")
add_probe_tests(normalized_par16_1_opb_msat normalized-par16-1.opb.msat.wcnf.xz
    "c probes 4032 failed 367 lifted 0 implied 57388.*c soft\-true 408 lower\-bound 408.*c block conflicting implied 0.*c greedy\-skipped 1196 weight 1196.*s UNKNOWN")
add_probe_tests(pcmax_scheduling_m37_28831_324346_SAT_sa pcmax-scheduling-m37-28831-324346-SAT.sanitized.cnf.xz
    "c probes 56574 failed 76 lifted 78 implied 697972.*c root\-assigned 581.*s UNKNOWN")
add_probe_tests(php15_mixed_15percent_blocked php15-mixed-15percent-blocked.cnf.xz
    "c probes 708 failed 0 lifted 0 implied 12408.*c root\-assigned 0.*s UNKNOWN")
add_probe_tests(rovers2_ks99i_renamed_as_sat05_3972 rovers2_ks99i.renamed-as.sat05-3972.cnf.xz
    "c probes 597 failed 38 lifted 0 implied 14452.*c root\-assigned 53.*s UNKNOWN")
add_probe_tests(satsgi_n28himBHm30_p0_q333 satsgi-n28himBHm30-p0-q333.cnf.xz
    "c probes 1680 failed 0 lifted 0 implied 47040.*c root\-assigned 0.*s UNKNOWN")
add_probe_tests(simp_b2ar_45_08 simp-b2ar_45.08.wcnf.xz
    "c probes 1854 failed 0 lifted 0 implied 10747.*c soft\-true 4 lower\-bound 12.*c block consistent implied 33.*c greedy\-skipped 12 weight 12.*s UNKNOWN")
add_probe_tests(sp5_21_15_bin_stri_flat_noid sp5-21-15-bin-stri-flat-noid.cnf.xz
    "c probes 8738 failed 0 lifted 0 implied 1836678.*c root\-assigned 1179.*s UNKNOWN")
add_probe_tests(SU2_simp_genos_haps_40 SU2__simp-genos.haps.40.wcnf.xz
    "c probes 5162 failed 46 lifted 143 implied 37033.*c soft\-true 13 lower\-bound 151.*c block consistent implied 106.*c greedy\-skipped 151 weight 151.*s UNKNOWN")
add_probe_tests(uaq_nr_nr120_nc40_n3_k2_rpp4_ppr2_plb50 uaq-nr-nr120-nc40-n3-k2-rpp4-ppr2-plb50.wcnf.xz
    "c probes 633 failed 0 lifted 0 implied 1889.*c soft\-true 0 lower\-bound 0.*c block conflicting implied 0.*c greedy\-skipped 57 weight 57.*s UNKNOWN")

# SLUR solves these mostly-Horn instances without giving up.
foreach(prop occ watch)
    foreach(k 1 2 -2)
        foreach(inst_seed "satsgi-n28himBHm30-p0-q333;3" "289-sat-14x8;1")
            list(GET inst_seed 0 inst)
            list(GET inst_seed 1 seed)
            add_test(NAME slursat.${inst}.k${k}.${prop}
                COMMAND slursat ${INSTANCES}/${inst}.cnf.xz -s ${seed} -k ${k} -p ${prop})
            set_tests_properties(slursat.${inst}.k${k}.${prop} PROPERTIES
                PASS_REGULAR_EXPRESSION "s SATISFIABLE" FAIL_REGULAR_EXPRESSION "ERROR"
                TIMEOUT 60 LABELS integration)
        endforeach()
    endforeach()
    # SLUR(k) gives up on the pigeon hole formula quickly, for every k.
    add_test(NAME slursat.php15.k3.${prop} COMMAND slursat ${INSTANCES}/php15-mixed-15percent-blocked.cnf.xz -s 1 -k 3 -p ${prop})
    set_tests_properties(slursat.php15.k3.${prop} PROPERTIES PASS_REGULAR_EXPRESSION "s UNKNOWN" TIMEOUT 60 LABELS integration)
endforeach()
