include(BundleUtilities)

function(gp_resolved_file_type_override resolved_file type)
	if("${resolved_file}" MATCHES "/*obs\\.[^/]*$")
		message(INFO " Excluding " ${resolved_file} " from prerequisite resolution.")
		set("${type}" "system" PARENT_SCOPE)
	endif()
endfunction(gp_resolved_file_type_override)

get_prerequisites("${TARGET_FILE}" PREREQUISITES 1 1 1 "" "")

set(PLUGIN_DESTINATION "${CMAKE_BINARY_DIR}/rundir/${CMAKE_BUILD_TYPE}/obs-plugins/${LIB_SUFFIX}bit")
foreach(PREREQUISITE ${PREREQUISITES})
	gp_resolve_item("${TARGET_FILE}" "${PREREQUISITE}" "" "" resolved_file)
	file(COPY "${resolved_file}" DESTINATION "${PLUGIN_DESTINATION}")
endforeach()
