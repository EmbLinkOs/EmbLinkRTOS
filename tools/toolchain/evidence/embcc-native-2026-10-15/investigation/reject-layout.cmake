function(embcc_mismatch_probe)
  target_compile_options(emb_host_abi PRIVATE -fpack-struct=1)
endfunction()
cmake_language(DEFER CALL embcc_mismatch_probe)
