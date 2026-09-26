; Deletes every value under ROOT\KEY whose name ends in "\MANIFEST", so only one
; registration of the layer remains (older installs, dev builds).
; Needs LogicLib. Clobbers $0 to $3.
!macro PurgeLayers ROOT KEY MANIFEST
  StrCpy $0 0
  StrLen $2 "\${MANIFEST}"
  IntOp $2 0 - $2
  ${Do}
    EnumRegValue $1 ${ROOT} "${KEY}" $0
    ${IfThen} $1 == "" ${|} ${ExitDo} ${|}
    StrCpy $3 $1 "" $2
    ${If} $3 == "\${MANIFEST}"
      DeleteRegValue ${ROOT} "${KEY}" $1  ; the next value shifts into index $0
    ${Else}
      IntOp $0 $0 + 1
    ${EndIf}
  ${Loop}
!macroend
