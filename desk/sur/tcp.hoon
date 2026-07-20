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
  $%  [%connect =wire =target timeout=(unit @ud)]
      [%send =wire data=octs]
      [%close =wire]
  ==
::  gifts: sidecar/runtime -> agent/vane
::
+$  gift
  $%  [%connected =wire]
      [%receive =wire data=octs]
      [%closed =wire]
      [%error =wire msg=@t]
  ==
--
