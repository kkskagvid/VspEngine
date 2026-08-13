cmake_minimum_required (VERSION 3.21)

project(icu4c-77_1)

option(BUILD_ICU_MONOLITHIC "Build ICU as a single monolithic library, rather than as several separate libraries" ON)

#set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")

set(ICU_MAJOR_VERSION 77)
set(ICU_SRC_ROOT ../source)
set(ICU_BIN_ROOT ${CMAKE_BINARY_DIR}/../)

set(ICU_LIBRARY_TYPE STATIC)
add_definitions(-DU_STATIC_IMPLEMENTATION)

set(ICU_LIBRARY_NAME icu)
set(ICU_DT_LIBRARY_NAME icudt)
set(ICU_UC_LIBRARY_NAME icuuc)
set(ICU_IN_LIBRARY_NAME icuin)
set(ICU_IO_LIBRARY_NAME icuio)

if (NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${ICU_BIN_ROOT})
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ICU_BIN_ROOT})
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ICU_BIN_ROOT})
endif()

add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-DU_ATTRIBUTE_DEPRECATED=)
add_definitions(-DUCONFIG_NO_FILE_IO)

include_directories(${ICU_SRC_ROOT}/common ${ICU_SRC_ROOT}/i18n)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")


set(ICU_DT_SOURCES 
	stubdata.cpp
)
set(TEMP_LIST "")
foreach(SOURCE_FILE ${ICU_DT_SOURCES})
	list(APPEND TEMP_LIST ${ICU_SRC_ROOT}/stubdata/${SOURCE_FILE})
endforeach(SOURCE_FILE)
set(ICU_DT_SOURCES ${TEMP_LIST})
set(TEMP_LIST "")


set(ICU_UC_SOURCES 
	ubidi.cpp ubidi_props.cpp ubidiln.cpp ubiditransform.cpp ubidiwrt.cpp ushape.cpp
	brkeng.cpp brkiter.cpp dictbe.cpp dictionarydata.cpp filteredbrk.cpp lstmbe.cpp mlbe.cpp rbbi.cpp rbbi_cache.cpp rbbidata.cpp rbbinode.cpp rbbirb.cpp rbbiscan.cpp rbbisetb.cpp rbbistbl.cpp rbbitblb.cpp ubrk.cpp
	ucol_swp.cpp
	bytestrie.cpp bytestriebuilder.cpp bytestrieiterator.cpp pluralmap.cpp propsvec.cpp stringtriebuilder.cpp uarrsort.cpp ucharstrie.cpp ucharstriebuilder.cpp ucharstrieiterator.cpp ucptrie.cpp uenum.cpp uhash.cpp uhash_us.cpp ulist.cpp umutablecptrie.cpp unifiedcache.cpp ustack.cpp ustrenum.cpp utrie.cpp utrie_swap.cpp utrie2.cpp utrie2_builder.cpp uvector.cpp uvectr32.cpp uvectr64.cpp
	errorcode.cpp icudataver.cpp locmap.cpp putil.cpp umath.cpp umutex.cpp utrace.cpp utypes.cpp wintz.cpp
	ucnv.cpp ucnv_bld.cpp ucnv_cb.cpp ucnv_cnv.cpp ucnv_ct.cpp ucnv_err.cpp ucnv_ext.cpp ucnv_io.cpp ucnv_lmb.cpp ucnv_set.cpp ucnv_u7.cpp ucnv_u8.cpp ucnv_u16.cpp ucnv_u32.cpp ucnv2022.cpp ucnvbocu.cpp ucnvdisp.cpp ucnvhz.cpp ucnvisci.cpp ucnvlat1.cpp ucnvmbcs.cpp ucnvscsu.cpp ucnvsel.cpp
	cmemory.cpp restrace.cpp sharedobject.cpp ucln_cmn.cpp ucmndata.cpp udata.cpp udatamem.cpp udataswp.cpp uinit.cpp umapfile.cpp uobject.cpp
	dtintrv.cpp locdspnm.cpp messagepattern.cpp parsepos.cpp simpleformatter.cpp static_unicode_sets.cpp ucurr.cpp ustrfmt.cpp util.cpp util_props.cpp
	punycode.cpp uidna.cpp uts46.cpp
	localebuilder.cpp localematcher.cpp localeprioritylist.cpp locavailable.cpp locbased.cpp locdispnames.cpp locdistance.cpp locid.cpp loclikely.cpp loclikelysubtags.cpp locresdata.cpp locutil.cpp lsr.cpp resbund.cpp resbund_cnv.cpp resource.cpp ucat.cpp uloc.cpp uloc_keytype.cpp uloc_tag.cpp ulocale.cpp ulocbuilder.cpp ures_cnv.cpp uresbund.cpp uresdata.cpp
	caniter.cpp filterednormalizer2.cpp loadednormalizer2impl.cpp normalizer2.cpp normalizer2impl.cpp normlzr.cpp unorm.cpp unormcmp.cpp
	bmpset.cpp characterproperties.cpp emojiprops.cpp patternprops.cpp propname.cpp ruleiter.cpp ucase.cpp uchar.cpp unames.cpp unifilt.cpp unifunct.cpp uniset.cpp uniset_closure.cpp uniset_props.cpp unisetspan.cpp uprops.cpp usc_impl.cpp uscript.cpp uscript_props.cpp uset.cpp uset_props.cpp usetiter.cpp
	icuplug.cpp serv.cpp servlk.cpp servlkf.cpp servls.cpp servnotf.cpp servrbf.cpp servslkf.cpp
	usprep.cpp
	appendable.cpp bytesinkutil.cpp bytestream.cpp chariter.cpp charstr.cpp cstr.cpp cstring.cpp cwchar.cpp edits.cpp schriter.cpp stringpiece.cpp ucasemap.cpp ucasemap_titlecase_brkiter.cpp uchriter.cpp uinvchar.cpp uiter.cpp unistr.cpp unistr_case.cpp unistr_case_locale.cpp unistr_cnv.cpp unistr_props.cpp unistr_titlecase_brkiter.cpp ustr_cnv.cpp ustr_titlecase_brkiter.cpp ustr_wcs.cpp ustrcase.cpp ustrcase_locale.cpp ustring.cpp ustrtrns.cpp utext.cpp utf_impl.cpp
)
set(TEMP_LIST "")
foreach(SOURCE_FILE ${ICU_UC_SOURCES})
	list(APPEND TEMP_LIST ${ICU_SRC_ROOT}/common/${SOURCE_FILE})
endforeach(SOURCE_FILE)
set(ICU_UC_SOURCES ${TEMP_LIST})
set(TEMP_LIST "")

set(ICU_IN_SOURCES
	erarules.cpp region.cpp uregion.cpp alphaindex.cpp bocsu.cpp coleitr.cpp coll.cpp
	collation.cpp collationbuilder.cpp collationcompare.cpp collationdata.cpp collationdatabuilder.cpp collationdatareader.cpp collationdatawriter.cpp
	collationfastlatin.cpp collationfastlatinbuilder.cpp collationfcd.cpp collationiterator.cpp collationkeys.cpp collationroot.cpp collationrootelements.cpp
	collationruleparser.cpp collationsets.cpp collationsettings.cpp collationtailoring.cpp collationweights.cpp rulebasedcollator.cpp search.cpp
	sortkey.cpp stsearch.cpp tzfmt.cpp tzgnames.cpp tznames.cpp tznames_impl.cpp ucol.cpp
	ucol_res.cpp ucol_sit.cpp ucoleitr.cpp uitercollationiterator.cpp usearch.cpp astro.cpp basictz.cpp
	buddhcal.cpp calendar.cpp cecal.cpp chnsecal.cpp choicfmt.cpp compactdecimalformat.cpp coptccal.cpp
	curramt.cpp currfmt.cpp currpinf.cpp currunit.cpp dangical.cpp datefmt.cpp dayperiodrules.cpp
	dcfmtsym.cpp decContext.cpp decimfmt.cpp decNumber.cpp double-conversion-bignum-dtoa.cpp double-conversion-bignum.cpp double-conversion-cached-powers.cpp
	double-conversion-double-to-string.cpp double-conversion-fast-dtoa.cpp double-conversion-string-to-double.cpp double-conversion-strtod.cpp dtfmtsym.cpp dtitvfmt.cpp dtitvinf.cpp
	dtptngen.cpp dtrule.cpp ethpccal.cpp fmtable.cpp fmtable_cnv.cpp format.cpp formattedval_iterimpl.cpp
	formattedval_sbimpl.cpp formattedvalue.cpp fphdlimp.cpp fpositer.cpp gender.cpp gregocal.cpp gregoimp.cpp
	hebrwcal.cpp indiancal.cpp islamcal.cpp iso8601cal.cpp japancal.cpp listformatter.cpp ulistformatter.cpp
	measfmt.cpp measunit.cpp measunit_extra.cpp measure.cpp messageformat2.cpp messageformat2_arguments.cpp messageformat2_checker.cpp
	messageformat2_data_model.cpp messageformat2_errors.cpp messageformat2_evaluation.cpp messageformat2_formattable.cpp messageformat2_formatter.cpp messageformat2_function_registry.cpp messageformat2_parser.cpp
	messageformat2_serializer.cpp msgfmt.cpp nfrs.cpp nfrule.cpp nfsubs.cpp number_affixutils.cpp number_asformat.cpp
	number_compact.cpp number_decimalquantity.cpp number_decimfmtprops.cpp number_fluent.cpp number_formatimpl.cpp number_grouping.cpp number_integerwidth.cpp
	number_longnames.cpp number_modifiers.cpp number_notation.cpp number_output.cpp number_padding.cpp number_patternmodifier.cpp number_patternstring.cpp
	number_rounding.cpp number_scientific.cpp formatted_string_builder.cpp number_usageprefs.cpp number_utils.cpp number_mapper.cpp number_multiplier.cpp
	number_currencysymbols.cpp number_simple.cpp number_skeletons.cpp number_symbolswrapper.cpp number_capi.cpp string_segment.cpp numparse_parsednumber.cpp
	numparse_impl.cpp numparse_symbols.cpp numparse_decimal.cpp numparse_scientific.cpp numparse_currency.cpp numparse_affixes.cpp numparse_compositions.cpp
	numparse_validators.cpp numrange_capi.cpp numrange_fluent.cpp numrange_impl.cpp numfmt.cpp numsys.cpp olsontz.cpp
	persncal.cpp pluralranges.cpp plurfmt.cpp plurrule.cpp quantityformatter.cpp rbnf.cpp rbtz.cpp
	reldatefmt.cpp reldtfmt.cpp scientificnumberformatter.cpp sharedbreakiterator.cpp selfmt.cpp simpletz.cpp scriptset.cpp
	smpdtfmt.cpp smpdtfst.cpp standardplural.cpp taiwncal.cpp timezone.cpp tmunit.cpp tmutamt.cpp
	tmutfmt.cpp tzrule.cpp tztrans.cpp ucal.cpp udat.cpp udateintervalformat.cpp udatpg.cpp
	ufieldpositer.cpp ulocdata.cpp umsg.cpp units_complexconverter.cpp units_converter.cpp units_data.cpp units_router.cpp
	displayoptions.cpp unum.cpp unumsys.cpp upluralrules.cpp utf16collationiterator.cpp utf8collationiterator.cpp utmscale.cpp
	vtzone.cpp vzone.cpp windtfmt.cpp winnmfmt.cpp wintzimpl.cpp zonemeta.cpp zrule.cpp
	ztrans.cpp ucln_in.cpp regexcmp.cpp regeximp.cpp regexst.cpp regextxt.cpp rematch.cpp
	repattrn.cpp uregex.cpp uregexc.cpp anytrans.cpp brktrans.cpp casetrn.cpp cpdtrans.cpp
	esctrn.cpp funcrepl.cpp name2uni.cpp nortrans.cpp nultrans.cpp quant.cpp rbt.cpp
	rbt_data.cpp rbt_pars.cpp rbt_rule.cpp rbt_set.cpp remtrans.cpp strmatch.cpp strrepl.cpp
	titletrn.cpp tolowtrn.cpp toupptrn.cpp translit.cpp transreg.cpp tridpars.cpp unesctrn.cpp
	uni2name.cpp utrans.cpp csdetect.cpp csmatch.cpp csr2022.cpp csrecog.cpp csrmbcs.cpp
	csrsbcs.cpp csrucode.cpp csrutf8.cpp inputext.cpp ucsdet.cpp uspoof.cpp uspoof_build.cpp
	uspoof_conf.cpp uspoof_impl.cpp
)
set(TEMP_LIST "")
foreach(SOURCE_FILE ${ICU_IN_SOURCES})
	list(APPEND TEMP_LIST ${ICU_SRC_ROOT}/i18n/${SOURCE_FILE})
endforeach(SOURCE_FILE)
set(ICU_IN_SOURCES ${TEMP_LIST})
set(TEMP_LIST "")


set(ICU_IO_SOURCES
	locbund.cpp sprintf.cpp sscanf.cpp ucln_io.cpp ufile.cpp ufmt_cmn.cpp uprintf.cpp
	uprntf_p.cpp uscanf.cpp uscanf_p.cpp ustdio.cpp ustream.cpp
)
set(TEMP_LIST "")
foreach(SOURCE_FILE ${ICU_IO_SOURCES})
	list(APPEND TEMP_LIST ${ICU_SRC_ROOT}/io/${SOURCE_FILE})
endforeach(SOURCE_FILE)
set(ICU_IO_SOURCES ${TEMP_LIST})
set(TEMP_LIST "")


# Building ICU as a monolithic library?
if (${BUILD_ICU_MONOLITHIC})
	# Create the icu (Monolithic) library
	add_library(${ICU_LIBRARY_NAME} ${ICU_LIBRARY_TYPE} ${ICU_DT_SOURCES} ${ICU_UC_SOURCES} ${ICU_IN_SOURCES} ${ICU_IO_SOURCES})
	if (${CMAKE_VERSION} VERSION_LESS 3.12)
		set_property(TARGET ${ICU_LIBRARY_NAME} PROPERTY CXX_STANDARD 11)
	else()
		target_compile_features(${ICU_LIBRARY_NAME} PRIVATE cxx_std_11) 
	endif()
	target_compile_definitions(${ICU_LIBRARY_NAME} PRIVATE -DU_COMMON_IMPLEMENTATION -DU_I18N_IMPLEMENTATION -DU_IO_IMPLEMENTATION)
else()
	# Create the icudt (Data) library
	add_library(${ICU_DT_LIBRARY_NAME} ${ICU_LIBRARY_TYPE} ${ICU_DT_SOURCES})
	target_compile_features(${ICU_DT_LIBRARY_NAME} PRIVATE cxx_std_11)

	# Create the icuuc (Unicode Common) library
	add_library(${ICU_UC_LIBRARY_NAME} ${ICU_LIBRARY_TYPE} ${ICU_UC_SOURCES})
	if (${CMAKE_VERSION} VERSION_LESS 3.12)
		set_property(TARGET ${ICU_UC_LIBRARY_NAME} PROPERTY CXX_STANDARD 11)
	else()
		target_compile_features(${ICU_UC_LIBRARY_NAME} PRIVATE cxx_std_11)
	endif()
	target_compile_definitions(${ICU_UC_LIBRARY_NAME} PRIVATE -DU_COMMON_IMPLEMENTATION)
	target_link_libraries(${ICU_UC_LIBRARY_NAME} ${ICU_DT_LIBRARY_NAME})

	# Create the icuin (Internationalization) library
	add_library(${ICU_IN_LIBRARY_NAME} ${ICU_LIBRARY_TYPE} ${ICU_IN_SOURCES})
	if (${CMAKE_VERSION} VERSION_LESS 3.12)
		set_property(TARGET ${ICU_IN_LIBRARY_NAME} PROPERTY CXX_STANDARD 11)
	else()
		target_compile_features(${ICU_IN_LIBRARY_NAME} PRIVATE cxx_std_11)
	endif()
	target_compile_definitions(${ICU_IN_LIBRARY_NAME} PRIVATE -DU_I18N_IMPLEMENTATION)
	target_link_libraries(${ICU_IN_LIBRARY_NAME} ${ICU_UC_LIBRARY_NAME})

	# Create the icuio (Input/Output) library
	add_library(${ICU_IO_LIBRARY_NAME} ${ICU_LIBRARY_TYPE} ${ICU_IO_SOURCES})
	if (${CMAKE_VERSION} VERSION_LESS 3.12)
		set_property(TARGET ${ICU_IO_LIBRARY_NAME} PROPERTY CXX_STANDARD 11)
	else()
		target_compile_features(${ICU_IO_LIBRARY_NAME} PRIVATE cxx_std_11)
	endif()
	target_compile_definitions(${ICU_IO_LIBRARY_NAME} PRIVATE -DU_IO_IMPLEMENTATION)
	target_link_libraries(${ICU_IO_LIBRARY_NAME} ${ICU_UC_LIBRARY_NAME} ${ICU_IN_LIBRARY_NAME})
endif()
