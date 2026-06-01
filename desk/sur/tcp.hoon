|%
::  network endpoint: address + port
::
+$  fief
  $%  [%turf p=(list turf) q=@ud]
      [%if p=@if q=@ud]
      [%is p=@is q=@ud]
  ==
::  connection target: endpoint + tls
::
+$  target  [secure=? =fief]
::  tasks: agent/vane -> sidecar/runtime
::
+$  task
  $%  [%connect =wire =target]
      [%send =wire data=@]
      [%close =wire]
  ==
::  gifts: sidecar/runtime -> agent/vane
::
+$  gift
  $%  [%connected =wire]
      [%receive =wire data=@]
      [%closed =wire]
      [%error =wire msg=@t]
  ==
--
