#!/usr/bin/env python3
"""Read-only correlation of nx2androidX capture TSVs. Never certifies a draw.

Handles separate capture directories in ZIPs; never joins Vulkan handles across
capture scopes. The surface catalog deduplicates bindings, records pass-end
attachments and lacks render-area offsets/viewport/fragment shader semantics.
All matches are candidates for review, not permission to substitute a draw.
"""
import argparse
import csv
import io
import json
import math
import zipfile
from collections import defaultdict, Counter, deque
from bisect import bisect_left
from pathlib import PurePosixPath

REQUIRED = {'role','present','submission','pass','draws','dispatches','slot','element','stage','shader','image','view','pool','write_sequence','aspect','mip','mips','layer','layers','width','height','samples','status','guest_offset','guest_bytes','storage_format','view_format','store'}
WRITES = {'color','depth','stencil','storage-binding','compute-image','copy-destination','buffer-copy-destination','texture-clear','texture-clear-output'}

def number(row, field, base=10):
    try: return int(row.get(field,'') or '0', base)
    except (ValueError, TypeError): return None

def resource(row):
    """Require exact color mip/layer and single-sample source, not just size."""
    im=number(row,'image',16); pool=number(row,'pool',16)
    mip=number(row,'mip'); layer=number(row,'layer')
    if not im or pool is None or mip is None or layer is None or min(mip,layer)<0:
        return None
    if (number(row,'aspect',16),number(row,'mips'),number(row,'layers'),number(row,'samples')) != (1,1,1,1):
        return None
    return im,pool,mip,layer

def extent(row):
    w,h,m=number(row,'width'),number(row,'height'),number(row,'mip')
    if w is None or h is None or m is None or w<=0 or h<=0 or not 0<=m<32: return None
    return max(1,w>>m),max(1,h>>m)

def overlaps(a,b):
    """Conservatively flag unknown-range writes and writes through pool aliases."""
    ai,bi=number(a,'image',16),number(b,'image',16)
    ap,bp=number(a,'pool',16),number(b,'pool',16)
    if ai and ai==bi:
        for axis,count in [('mip','mips'),('layer','layers')]:
            x,y,n,m=(number(a,axis),number(b,axis),number(a,count),number(b,count))
            if None not in (x,y,n,m) and n>0 and m>0 and (x+n<=y or y+m<=x): return False
        return True
    if ap and ap==bp:
        x,y,n,m=(number(a,'guest_offset'),number(b,'guest_offset'),number(a,'guest_bytes'),number(b,'guest_bytes'))
        if None in (x,y,n,m) or min(n,m)<=0: return True
        return x<y+m and y<x+n
    return False

def read_tsv(blob):
    text=blob.decode('utf-8-sig',errors='strict')
    lines=text.splitlines(); metadata=[s for s in lines if s.startswith('#')]
    reader=csv.DictReader((s for s in lines if s and not s.startswith('#')),delimiter='\t')
    rows=list(reader)
    if any(None in r or any(v is None for v in r.values()) for r in rows): raise ValueError('malformed TSV row')
    return set(reader.fieldnames or ()),rows,metadata

def context(row):
    return tuple(number(row,k) for k in ('present','submission','pass'))

def presentation_links(rows, groups, writes):
    """Trace possible color dependencies to the actual presentation resource.

    A pass-level dependency is not proof that its fragment shader consumes a
    binding in the relevant draw, nor that the binding supplies scene color.
    Do not infer a relationship across capture presents, aliased writes,
    unknown subresources, or missing/ambiguous presentation markers.
    """
    markers=defaultdict(list)
    for i,r in enumerate(rows):
        if r['role']=='presentation-source': markers[context(r)[0]].append(i)
    # Limit work on adversarial or unusually large catalogs. Incomplete paths
    # remain unlinked; no candidate is ever certified by this analysis.
    links={}; reports={}
    for present, ids in markers.items():
        reports[present]={'markers':[i+1 for i in ids], 'truncated':False, 'linked_writes':0}
        report=reports[present]
        if len(ids)!=1 or not resource(rows[ids[0]]) or not number(rows[ids[0]],'write_sequence'):
            report['reason']='ambiguous or unresolved presentation source'; continue
        marker=ids[0]
        ticket=number(rows[marker],'real_ticket')
        if ticket is not None and ticket>0: report['real_ticket']=ticket
        # Index possible overlapping writes by image or pool within this frame.
        indexes=defaultdict(list)
        for i,r in writes[present]:
            for field in ('image','pool'):
                value=number(r,field,16)
                if value: indexes[(field,value)].append(i)
        remaining=1_000_000
        def last_writer(read, *, load=False):
            nonlocal remaining
            r=rows[read]
            if not resource(r) or not number(r,'write_sequence'): return None
            latest=-1
            for field in ('image','pool'):
                value=number(r,field,16)
                indices=indexes.get((field,value),())
                at=bisect_left(indices,read)-1
                while at>=0 and indices[at]>latest:
                    remaining-=1
                    if remaining<0: report['truncated']=True; return None
                    i=indices[at]; at-=1
                    if overlaps(r,rows[i]): latest=i; break
            if latest<0: return None
            writer=rows[latest]
            if writer['role'] not in ('color','compute-image','copy-destination','buffer-copy-destination'):
                return None
            if resource(writer)!=resource(r): return None
            before, after = number(writer,'write_sequence'), number(r,'write_sequence')
            if load:
                # A LOAD/STORE pass may preserve previous contents, but its
                # pass-end sequence identifies the NEW write, not the input.
                # Require strictly earlier exact writer; reject aliases,
                # discarded writes, same-pass writes, and format/extent changes.
                if (writer['role']!='color' or not before or before>=after or
                    context(writer)==context(r) or extent(writer)!=extent(r) or
                    writer['storage_format']!=r['storage_format'] or
                    writer['view_format']!=r['view_format']): return None
            elif before!=after: return None
            if writer['role']=='color' and number(writer,'store')!=0: return None
            return latest
        final=last_writer(marker)
        if final is None:
            report['reason']='no exact last writer for presentation source'; continue
        queue=deque([(final,(final,marker))]); visited=set(); discovered={final}
        while queue and not report['truncated']:
            if len(visited)>=4096: report['truncated']=True; break
            i,path=queue.popleft()
            if i in visited: continue
            visited.add(i)
            links[i]=[j+1 for j in path]
            out=rows[i]
            # Only graphics color writes can be traced through sampled fragment
            # bindings with this catalog. Compute/transfer pairing is ambiguous.
            if out['role']!='color': continue
            # Pass-end attachment rows also encode an implicit color input.
            # LOAD is a possible dependency, not proof that any pixel survives
            # the draw(s); never follow CLEAR, DONT_CARE or missing load fields.
            if out.get('load')=='0':
                producer=last_writer(i,load=True)
                if producer is not None and producer not in discovered:
                    discovered.add(producer)
                    queue.append((producer,(producer,)+path))
            for j,r in groups[context(out)]:
                remaining-=1
                if remaining<0: report['truncated']=True; break
                if (j>=i or r['role']!='sampled-binding' or r['status']!='metadata' or
                    number(r,'stage')!=1 or not number(r,'view',16) or not resource(r) or overlaps(r,out)):
                    continue
                producer=last_writer(j)
                if producer is None or context(rows[producer])==context(out): continue
                # A later overlapping write during this pass makes the binding
                # ambiguous; the catalog cannot prove which draw sampled it.
                blocked=False
                for field in ('image','pool'):
                    indices=indexes.get((field,number(r,field,16)),())
                    at=bisect_left(indices,j+1)
                    while at<len(indices) and indices[at]<i:
                        remaining-=1
                        if remaining<0: report['truncated']=True; blocked=True; break
                        if overlaps(r,rows[indices[at]]): blocked=True; break
                        at+=1
                    if blocked: break
                if not blocked and producer not in discovered:
                    if len(discovered)>=4096: report['truncated']=True; break
                    discovered.add(producer)
                    queue.append((producer,(producer,j)+path))
        report['linked_writes']=len(visited)
        report['reason']='pass-level resource dependency only; draw semantics not certified'
    return links,reports

def analyze(rows, max_candidates=200, shader_index=None):
    groups=defaultdict(list); writes=defaultdict(list); linked_results=[]; other_results=[]; stats=Counter()
    for i,r in enumerate(rows):
        if None in context(r): raise ValueError('invalid present/submission/pass')
        groups[context(r)].append((i,r))
        if r['role'] in WRITES: writes[context(r)[0]].append((i,r))
    links,presentation_reports=presentation_links(rows,groups,writes)
    for ctx,entries in groups.items():
        colors=[(i,r) for i,r in entries if r['role']=='color' and resource(r) and extent(r)]
        reads=[(i,r) for i,r in entries if r['role']=='sampled-binding' and r['status']=='metadata' and number(r,'stage')==1 and number(r,'view',16) and resource(r) and extent(r)]
        areas=[r for _,r in entries if r['role']=='render-area']
        for j,src in reads:
            for k,dst in colors:
                a,b=extent(src),extent(dst)
                if a==b or a[0]>b[0] or a[1]>b[1]: continue
                if resource(src)==resource(dst) or overlaps(src,dst):
                    stats['alias_rejected']+=1; continue
                prior=[(i,r) for i,r in writes[ctx[0]] if i<j and overlaps(src,r)]
                if not prior:
                    stats['no_same_present_producer']+=1; continue
                i,producer=prior[-1]
                if (producer['role'] not in ('color','compute-image','copy-destination','buffer-copy-destination') or
                    resource(producer)!=resource(src) or number(producer,'write_sequence')!=number(src,'write_sequence') or
                    not number(src,'write_sequence')):
                    stats['unverified_last_write']+=1; continue
                if context(producer)==ctx or k<=j:
                    stats['invalid_order']+=1; continue
                # A matching write sequence and exact subresource do not prove coverage.
                reasons=['shader semantics and sampling coordinates not certified',
                         'valid source rectangle not certified',
                         'binding rows may be deduplicated across draws',
                         'HUD/composition/transfer function not certified']
                if len(colors)!=1: reasons.append('multiple color attachments')
                if len({(resource(r),r['shader'],r['slot'],r['element']) for _,r in reads})!=1:
                    reasons.append('multiple sampled bindings')
                if any(r['role'] in ('storage-binding','depth','stencil') for _,r in entries):
                    reasons.append('depth/stencil/storage participation')
                if len(areas)!=1: reasons.append('missing or multiple render areas')
                elif not {'render_x','render_y'}<=areas[0].keys(): reasons.append('render-area origin absent')
                elif (number(areas[0],'width'),number(areas[0],'height'))!=b:
                    reasons.append('render area differs from output view extent')
                if number(producer,'store')!=0 and producer['role']=='color': reasons.append('producer store not STORE')
                if any(i2>j and i2<k and overlaps(src,r2) for i2,r2 in writes[ctx[0]]):
                    reasons.append('potential intervening source write')
                states=[r for _,r in entries if r['role']=='draw-state' and r['draws']==src['draws'] and r['shader']==src['shader']]
                if len(states)!=1: reasons.append('exact draw-state row unavailable or ambiguous')
                if shader_index is None or src['shader'].lower() not in shader_index:
                    reasons.append('shader index entry unavailable; single-sampler draws may be omitted by index')
                downstream=[]
                for next_ctx,next_entries in groups.items():
                    if next_ctx[0]!=ctx[0] or next_ctx==ctx: continue
                    for n,consumer in next_entries:
                        if n<=k or consumer['role']!='sampled-binding' or resource(consumer)!=resource(dst): continue
                        if number(consumer,'write_sequence')!=number(dst,'write_sequence'): continue
                        if any(k<widx<n and overlaps(dst,wr) for widx,wr in writes[ctx[0]]): continue
                        downstream.append({'row':n+1,'binding':consumer})
                        if len(downstream)>=16: break
                    if len(downstream)>=16: break
                stats['candidates']+=1
                link={'linked':k in links, 'path_rows':links.get(k,[]),
                      'reason':('pass-level resource dependency only; not certified' if k in links else
                                'no exact dependency path established to presentation source' if ctx[0] in presentation_reports else
                                'no presentation source in this captured frame')}
                if k in links: stats['presentation_linked_candidates']+=1
                selected=linked_results if k in links else other_results
                if len(selected)<max_candidates:
                    selected.append(dict(certified=False, capture_present=ctx[0], submission=ctx[1], pass_index=ctx[2],
                        source_extent=a, destination_extent=b, producer_row=i+1, sampled_row=j+1, destination_row=k+1,
                        producer=producer, sampled=src, destination=dst, render_areas=areas,
                        draw_state=states, static_shader_evidence=(shader_index or {}).get(src['shader'].lower()), downstream=downstream, presentation_link=link, reasons=reasons))
    results=(linked_results+other_results)[:max_candidates]
    return dict(rows=len(rows), counts=dict(stats), candidates=results, presentation_sources=presentation_reports,
                truncated=stats['candidates']>len(results))

def summarize_presentation(rows, metadata):
    """Separate real-frame results from delayed reconstruction measurements."""
    def operation(row):
        return {'0':'handheld','1':'docked'}.get(row.get('docked_at_enqueue'), 'unknown')
    def value(row, key):
        try:
            v=float(row.get(key, ''))
            return v if math.isfinite(v) and v>=0 else None
        except (ValueError,TypeError):
            return None
    def stats(values):
        values=sorted(values)
        return {'samples':len(values),'mean':sum(values)/len(values),
                'p95':values[math.ceil(.95*len(values))-1],'max':values[-1]} if values else {'samples':0}
    frames={}; duplicates=set()
    for r in rows:
        ticket=number(r,'ticket')
        if r.get('presentation_seen')!='1' or ticket is None or ticket<=0: continue
        if ticket in frames:
            if frames[ticket]!=r: duplicates.add(ticket)
        else: frames[ticket]=r
    for ticket in duplicates: frames.pop(ticket,None)
    groups=defaultdict(list)
    for r in frames.values():
        groups[(operation(r),r.get('mode','unknown'),r.get('requested_upscaler','unknown'))].append(r)
    residency=[]
    for (op,internal,requested),rs in sorted(groups.items()):
        counts=Counter(r.get('active_upscaler','unknown') for r in rs)
        allocations=[v for r in rs if (v:=value(r,'reconstruction_allocations')) is not None]
        reset_flags=[v for r in rs if (v:=value(r,'source_reset_flags')) is not None]
        residency.append({'operation_mode':op,'internal_mode':internal,'requested':requested,
            'real_frames':len(rs),'effective_counts':dict(sorted(counts.items())),
            'requested_residency_pct':100*counts[requested]/len(rs),
            'allocation_wait_ms':stats([v/1e6 for r in rs if (v:=value(r,'reconstruction_alloc_wait_ns')) is not None]),
            'slot_wait_ms':stats([v/1e6 for r in rs if (v:=value(r,'reconstruction_slot_wait_ns')) is not None]),
            'allocations':sum(allocations) if allocations else None,
            'source_reset_frames':sum(v>0 for v in reset_flags) if reset_flags else None})
    bypasses = Counter()
    integrity = defaultdict(list)
    for ticket, row in frames.items():
        requested = row.get('requested_upscaler')
        active = row.get('active_upscaler')
        if requested not in (None, 'None', '-') and active == 'None':
            bypasses[operation(row), requested, row.get('reason') or 'unspecified'] += 1
        # Older captures wrote the guest crop into actual_reconstruction on
        # bypass. Only apply extent checks to the explicit submission schema.
        submitted = row.get('reconstruction_submitted')
        if submitted is None:
            continue
        actual = (number(row, 'actual_reconstruction_w'), number(row, 'actual_reconstruction_h'))
        target = (number(row, 'output_w'), number(row, 'output_h'))
        final = (number(row, 'final_source_w'), number(row, 'final_source_h'))
        if submitted not in ('0', '1'):
            integrity['invalid_submission_status'].append(ticket)
        elif submitted == '1':
            if active in (None, 'None', '-'):
                integrity['submitted_without_effective_backend'].append(ticket)
            if not all(dimension and dimension > 0 for dimension in actual) or actual != target:
                integrity['submitted_extent_mismatch'].append(ticket)
            if final != actual:
                integrity['final_source_extent_mismatch'].append(ticket)
        else:
            if active not in (None, 'None', '-'):
                integrity['effective_backend_without_submission'].append(ticket)
            if actual != (0, 0):
                integrity['output_extent_without_submission'].append(ticket)
    samples={}; conflicts=set(); no_backend=0
    for t,r in frames.items():
        origin=number(r,'reconstruction_timing_ticket')
        if origin is None or not 0<origin<t: continue
        backend=r.get('timing_backend','')
        if not backend or backend=='-':
            no_backend+=1; continue  # never infer from the current frame's backend
        gpu=value(r,'reconstruction_gpu_ms')
        if gpu is None: continue
        geometry=tuple(r.get(k,'') for k in ('timing_iw','timing_ih','timing_ow','timing_oh'))
        stages=tuple(value(r,k) for k in ('stage_input_ms','stage_motion_ms','stage_accumulate_ms','stage_output_ms'))
        sample=(backend,geometry,gpu,stages,r.get('timing_history_reset',''))
        if origin in samples and samples[origin]!=sample: conflicts.add(origin)
        else: samples[origin]=sample
    for t in conflicts: samples.pop(t,None)
    timing_groups=defaultdict(list)
    for t,sample in samples.items():
        backend,geometry,*_=sample
        timing_groups[(operation(frames.get(t,{})),backend,geometry)].append(sample)
    timings=[]
    for (op,backend,geometry),rs in sorted(timing_groups.items()):
        timings.append({'operation_mode':op,'backend':backend,'geometry':geometry,
            'gpu_ms':stats([r[2] for r in rs]),
            'history_initialization_samples':sum(r[4]=='1' for r in rs) if backend=='GSR 2' else None,
            'stages_ms':{name:stats([r[3][i] for r in rs if r[3][i] is not None]) for i,name in enumerate(('input','motion','accumulation','output'))} if backend=='GSR 2' else None})
    build_ids=[line.split('=',1)[1] for line in metadata if line.startswith('# runtime_elf_build_id=')]
    return {'runtime_elf_build_ids':build_ids,'residency':residency,'timings':timings,
        'conflicting_real_tickets':len(duplicates),'conflicting_timing_tickets':len(conflicts),
        'timing_rows_missing_backend':no_backend,
        'bypasses': [{'operation_mode': op, 'requested': backend, 'reason': reason, 'frames': count}
                     for (op, backend, reason), count in sorted(bypasses.items())],
        'submission_integrity': dict(sorted(integrity.items()))}

def inspect(path, limit):
    report={'archive':str(path),'scopes':[],'scaling':[],'warnings':[]}
    scaling_tables=[]
    with zipfile.ZipFile(path) as z:
        names=z.namelist()
        index_names=[n for n in names if PurePosixPath(n).name=='temporal-shaders.json']
        shader_index={}
        if len(index_names)==1:
            index_info=z.getinfo(index_names[0])
            if index_info.file_size>32*1024*1024: raise ValueError('shader index exceeds bounded read limit')
            index=json.loads(z.read(index_info))
            if index.get('schema')==1:
                shader_index={x['key'].lower():x for x in index.get('candidates',[]) if x.get('stage')==1}
                report['shader_index']={'path':index_names[0],'bundle_sha256':index.get('bundle_sha256'),'fragment_entries':len(shader_index)}
                report['warnings'].append('Shader index association is by key only; match bundle/runtime build identity before trusting semantics.')
        elif len(index_names)>1: report['warnings'].append('Multiple shader indexes; no automatic association.')
        for entry in z.infolist():
            name=entry.filename; leaf=PurePosixPath(name).name
            if leaf not in ('surface_usage.tsv','scaling_frames.tsv'): continue
            if entry.file_size>128*1024*1024: raise ValueError('TSV exceeds bounded read limit: '+name)
            fields,rows,metadata=read_tsv(z.read(entry))
            if leaf=='surface_usage.tsv':
                if not REQUIRED<=fields: raise ValueError('missing columns: '+str(REQUIRED-fields))
                result=analyze(rows,limit,shader_index); result['path']=name;result['metadata']=metadata
                result['capture_complete_marker']=str(PurePosixPath(name).parent/'complete') in names
                report['scopes'].append(result)
            else:
                if not {'ticket','mode','requested_upscaler','active_upscaler','presentation_seen'}<=fields:
                    raise ValueError('unexpected scaling_frames schema')
                scaling_tables.append((rows,metadata))
                counts=Counter((r['mode'],r['requested_upscaler'],r['active_upscaler']) for r in rows if r['presentation_seen']=='1')
                report['scaling'].append({'path':name,'metadata':metadata,'counts':[{'internal_mode':a,'requested':b,'active':c,'frames':n} for (a,b,c),n in sorted(counts.items())], 'summary':summarize_presentation(rows,metadata)})
    # An explicit ticket is the only allowed join key. A legacy replay present
    # counter is not interchangeable with the present queue's real-frame ticket.
    if len(scaling_tables)==1:
        rows,metadata=scaling_tables[0]
        by_ticket=defaultdict(list)
        for r in rows:
            ticket=number(r,'ticket')
            if ticket and r.get('presentation_seen')=='1': by_ticket[ticket].append(r)
        build_ids=[line.split('=',1)[1] for line in metadata if line.startswith('# runtime_elf_build_id=')]
        for scope in report['scopes']:
            if not scope['capture_complete_marker']: continue
            for source in scope['presentation_sources'].values():
                matched=by_ticket.get(source.get('real_ticket'),[])
                if len(matched)==1:
                    source['presentation_row']=matched[0]
                    if len(build_ids)==1 and build_ids[0] not in ('','unavailable'):
                        source['runtime_elf_build_id']=build_ids[0]
    elif len(scaling_tables)>1:
        report['warnings'].append('Multiple scaling tables; no automatic real-ticket join.')
    if not report['scopes']: report['warnings'].append('No surface_usage.tsv: capture full GPU diagnostics, not scaling-only.')
    report['warnings'].append('Legacy surface present counters are never equated with real-frame tickets. Explicit ticket joins still require matching runtime identity and draw semantics before acceptance.')
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('archives',nargs='+');p.add_argument('--max-candidates',type=int,default=200);p.add_argument('-o','--output');a=p.parse_args()
    if not 1<=a.max_candidates<=10000: p.error('max-candidates must be 1..10000')
    result=json.dumps([inspect(path,a.max_candidates) for path in a.archives],indent=2)+'\n'
    if a.output:
        with open(a.output,'w',encoding='utf-8') as f: f.write(result)
    else: print(result,end='')
if __name__=='__main__': main()
