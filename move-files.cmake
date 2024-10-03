
function (create_move_target name src dest)
    if(EXISTS ${src})
        configure_file(${src} ${dest} COPYONLY)
    endif()
endfunction()
