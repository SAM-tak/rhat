"""L^ syntax alignment, evaluation order, and ownership regressions."""
from harness import Suite


def main():
    suite = Suite('syntax')
    source, positive, negative = suite.source, suite.positive, suite.negative

    def versioned(rir, ll):
        assert '; range.guard result=' in ll

    positive('ref_keyword', '''
var^ n=7
var^ r:ref^ i32^=ref^n
var^ rr:ref^^ i32^=ref^r;
**rr:=9
let^ rrr:ref^^^ i32^=ref^rr
print^(***rrr)
let^ read=f^ p:ref^ i32^ ->i32^ {*p}
var^ refs:(ref^ i32^)[2]=[ref^ n,r]
let^ all:ref^ (ref^ i32^)[2]=ref^ refs
print^(read(ref^ n),*all[0],*refs[1],n*2)
def^ Box {self^{value:i32^=1} get=f^ self^ {self^.value}}
var^ box=Box{}
let^ b=ref^ box
b.value:=3
print^(b.value,b.get())
var^ values:i32^[2]=[4,5]
let^ a=ref^ values
print^(a[1],a.len)
let^ ref=11
print^(ref)
''', '9\n9 9 9 18\n3 3\n5 2\n11\n')

    positive('contextual_records', '''
def^ Point {self^{x:i32^=10,y:i32^=20}}
def^ Outer {self^{point:Point}}
let^ show=p^ x:Point {print^(x.x,x.y)}
show({x=1,y=2})
show({})
let^ a:Point={x=3}
show(a)
let^ nested:Outer={point={x=4,y=5}}
show(nested.point)
let^ make=f^ ->Point {return^ {x=6,y=7}}
let^ implicit=f^ ->Point {{x=8,y=9}}
show(make()) show(implicit())
var^ items:Point[2]=[{x=11},{y=12}]
show(items[0]) show(items[1])
let^ optional:Point|nil^={x=13,y=14}
print^(optional?.x??0)
let^ flag=f^ x:Point ->bool^ {x.x>0}
if^ flag({x=1}) {print^(99)}
let^ nestedFlag=f^ item:Outer {item.point.x>0}
if^ nestedFlag({point=Point{x=1}}) {print^(88)}
''', '1 2\n10 20\n3 20\n4 5\n6 7\n8 9\n11 20\n10 12\n13\n99\n88\n')

    positive('do_blocks', '''
var^ x=1
do^{}
do^{var^ x=2 print^(x) do^{var^ x=3 print^(x)}}
print^(x)
let^ early=p^ ->i32^ {do^{return^ 7}}
print^(early())
repeat^ 1 {if^ true^ {do^{next^}} print^(99)}
''', '2\n3\n1\n7\n')

    positive('contextual_owned_records', '''
def^ Bag {self^{tag:i32^=0,items:grow^ i32^[]}}
let^ make=f^ ->Bag {return^ {items=[1,2],tag=7}}
var^ bag=make()
print^(bag.tag,bag.items[1])
let^ read=f^ b:ref^ Bag ->i32^ {b.tag}
print^(read(ref^ bag))
''', '7 2\n7\n')

    positive('branches', '''
let^ choose=f^ x:i32^ {
 if^ x=0 { return^ 10
 elseif^ x=1: return^ 11
 else^if^ x=2: return^ 12
 elsif^ x=3: return^ 13
 elif^ x=4: return^ 14
 ei^ x=5: return^ 15
 el^ x=6: return^ 16
 else^: return^ 17
 }
}
for^ i from^ 0 to^ 7 { print^(choose(i)) }
print^(if^ false^: 1 elseif^ true^: 2 else^: 3;)
''', '10\n11\n12\n13\n14\n15\n16\n17\n2\n')

    positive('void_return_clauses', '''
let^ exit=p^ choice:bool^ { if^ choice { return^ else^: print^(1) } }
exit(true^) exit(false^)
let^ early=p^ { repeat^ 1 { pre^: return^ main^: print^(99) epilog^: print^(98) } }
early()
''', '1\n')
    positive('return_eof', 'return^', '')

    positive('outer_this', '''
print^(f^ n:i64^ {
 if^ n<2: 1 el^: n * f^ m:i64^ { this^^(m) }(n-1);
}(6))
let^ walk=f^ n:i32^ ->i32^ {
 if^ n=0 {return^ 0}
 return^ f^ m:i32^ ->i32^ {
   do^{ return^ f^ q:i32^ ->i32^ {this^^^(q-1)}(m) }
 }(n)+1
}
print^(walk(5))
print^(f^ n:i64^ {
 if^ n<2: 1 el^: f^ m:i64^ {if^ m=0: 0 el^: 1+this^(m-1);}(2)*this^(n-1);
}(4))
''', '720\n5\n8\n')

    positive('outer_this_owned', '''
let^ make=f^ n:i32^ ->grow^ i32^[] {
 if^ n=0 {return^ [7,8]}
 return^ f^ m:i32^ ->grow^ i32^[] {this^^(m-1)}(n)
}
var^ result=make(4)
print^(result.len,result[1])
let^ fill=p^ a:ref^ grow^ i32^[],n:i32^ ->i64^ {
 if^ n=0 {return^ a.len}
 return^ p^ b:ref^ grow^ i32^[],m:i32^ ->i64^ {b.push(m) return^ this^^(b,m-1)}(a,n)
}
var^ values:grow^ i32^[]=[]
print^(fill(ref^ values,3),values[0],values[2])
''', '2 8\n3 3 1\n')

    positive('outer_this_method', '''
def^ Counter {
 self^{base:i32^=3}
 sum=f^ self^,n:i32^ ->i32^ {
  if^ n=0 {return^ self^.base}
  return^ f^ obj:ref^ Counter,m:i32^ ->i32^ {this^^(obj,m-1)}(self^,n)+1
 }
 current=f^ self^,n:i32^ ->i32^ {if^ n=0: self^.base el^: 1+this^(n-1);}
}
def^ Derived:Counter {self^{marker:i32^=77}}
var^ value=Derived.new()
print^(value.sum(4),value.current(2),value.marker)
def^ Mutable {
 self^{count:i32^=0}
 run=p^ self^,n:i32^ ->i32^ {
  if^ n=0 {return^ self^.count}
  self^.count+=1
  return^ p^ obj:ref^ Mutable,m:i32^ ->i32^ {return^ this^^(obj,m-1)}(self^,n)
 }
}
var^ counter=Mutable.new()
print^(counter.run(4))
''', '7 5 77\n4\n')

    positive('assignments', '''
let^ index=p^ calls:ref^ i32^ ->i64^ { *calls+=1 return^ 0 }
let^ change=p^ a:ref^ i32^[2] ->i32^ { a[0]:=100 return^ 3 }
var^ calls=0
var^ a:i32^[2]=[10,20]
a[index(ref^ calls)] += change(ref^ a)
print^(a[0],calls)
a[0],a[1] := a[1],a[0]
print^(a[0],a[1])
var^ x,y=5,7
x,y += y,x
x-=2 x*=3 x/=2 x%=7
print^(x,y)
do^{ let^ x,y=40,x print^(x,y) }
var^ z:=9
print^(z)
''', '13 1\n20 13\n1 12\n40 1\n9\n')

    positive('comparison_chains', '''
let^ tick=p^ calls:ref^ i32^ ->i32^ { *calls+=1 return^ *calls }
var^ calls=0
print^(0 < tick(ref^ calls) < 2)
print^(calls)
print^(10 < tick(ref^ calls) < tick(ref^ calls))
print^(calls)
print^(1 <= 2 = 2 != 3)
print^(1 ≦ 2 and^ 3 ≥ 2 and^ 2 ≠ 3 and^ 1 =/ 2)
''', 'true\n1\nfalse\n2\ntrue\ntrue\n')

    positive('parallel_owned_result', '''
let^ make=f^ ->grow^ i32^[] {
 var^ n,data:grow^ i32^[]=7,[1,2]
 data.push(n)
 return^ data
}
var^ values=make()
let^ _^,answer=make(),42
print^(values.len,values[2],answer)
''', '3 7 42\n')

    positive('multiple_results', '''
let^ divmod=f^ a:i64^,b:i64^ ->(i64^,i64^) { return^ a/b,a%b }
let^ pair=f^ { 3,4.5 }
let^ swap=f^ a:string^,b:string^ { b,a }
let^ q,r=divmod(17,5)
var^ x,y=pair()
x,y:=pair()
x+=1
let^ s,t=swap("a","b")
let^ _^,last=divmod(9,4)
let^ again=f^ ->(i64^,i64^) { return^ divmod(7,2) }
let^ m,n=again()
let^ k:f^ i64^,i64^ ->(i64^,i64^); = divmod
let^ u,v=k(9,2)
print^(q,r,x,y,s,t,last,m,n,u,v)
def^ P { self^{ x:i64^=1, y:i64^=2 }
 both=f^ self^ ->(i64^,i64^) { self^.x,self^.y } }
let^ p=P{}
let^ a,b=p.both()
print^(a,b)
''', '3 2 4 4.5 b a 1 3 1 4 1\n1 2\n')

    positive('multiple_results_union', '''
errordef^ Error { Zero, Bad }
let^ dm=f^ a:i64^,b:i64^ ->(i64^,i64^) | Error.Zero {
 if^ b=0 { return^ Error.Zero{} }
 return^ a/b,a%b
}
let^ q,r=dm(7,0) catch^ (0,0)
let^ s,u=dm(7,2) catch^ (0,0)
let^ use=f^ a:i64^,b:i64^ ->i64^ | Error.Zero {
 let^ x,y=try^ dm(a,b)
 return^ x+y
}
let^ g=f^ b:i64^ { if^ b=0 { return^ Error.Bad{} } return^ b,b+1 }
let^ x,y=g(3) catch^ (9,9)
let^ z,w=g(0) catch^ (9,9)
let^ find=f^ n:i64^ ->(i64^,string^) | nil^ {
 if^ n<0 { return^ nil^ }
 return^ n*2,"ok"
}
let^ a,b=find(4) ?? (0,"none")
let^ c,d=find(-1) ?? (0,"none")
print^(q,r,s,u,use(7,2) catch^ -1,use(7,0) catch^ -1,x,y,z,w,a,b,c,d)
let^ v=dm(7,2)
if^ !(v fits^ Error.Zero) { let^ m,n=v print^(m,n) }
do^{ let^ m,n=try^ dm(7,0) print^(m,n) catch^: print^("caught") }
''', '0 0 3 1 4 -1 3 4 9 9 8 ok 0 none\n3 1\ncaught\n')

    positive('owned_unions', '''
errordef^ E { Bad }
let^ make=f^ n:i64^ ->grow^ i64^[] | E.Bad {
 if^ n<0 { return^ E.Bad{} }
 var^ xs:grow^ i64^[]=[]
 xs.push(n) xs.push(n+1)
 return^ xs
}
let^ use=f^ n:i64^ ->i64^ | E.Bad {
 var^ ys=try^ make(n)
 ys.push(9)
 return^ ys[0]+ys[2]
}
print^(use(3) catch^ -1,use(-1) catch^ -1)
var^ a=make(4) catch^ grow^[0]
let^ b=make(-1) catch^ grow^[7,7]
a.push(1)
print^(a.len,a[2],b.len,b[0])
let^ literal=f^ n:i64^ { if^ n<0 { return^ E.Bad{} } return^ grow^[n,n] }
let^ again=f^ n:i64^ ->grow^ i64^[] | E.Bad { return^ make(n) }
let^ x=literal(5) catch^ grow^[0]
let^ z=again(8) catch^ grow^[0]
print^(x.len,z[1])
let^ find=f^ n:i64^ ->grow^ i64^[] | nil^ { if^ n=0 { return^ nil^ } return^ grow^[n] }
let^ p=find(2) ?? grow^[0,0]
let^ q=find(0) ?? grow^[0,0]
print^(p.len,q.len)
var^ v=make(2)
if^ !(v fits^ E.Bad) { print^(v.len,v[1]) }
if^ v fits^ grow^ i64^[] { v.push(5) }
if^ v fits^ grow^ i64^[] { print^(v.len) }
var^ total:i64^=0
repeat^ 100000 { let^ t=make(1) catch^ grow^[0] total+=t[1] }
print^(total)
def^ A { self^{ data:grow^ u8^[]=[] } }
def^ B {}
def^ Value = A | B
let^ w:Value=B{}
let^ none:grow^ i32^[] | nil^=nil^
print^(w fits^ B,none fits^ nil^)
let^ count=f^ n:i64^ ->(i64^,grow^ i64^[]) | E.Bad {
 if^ n<0 { return^ E.Bad{} }
 var^ xs:grow^ i64^[]=[]
 xs.push(n) xs.push(n*10)
 return^ n*2,xs
}
let^ k,ys=count(3) catch^ (0,grow^[0])
let^ j,zs=count(-1) catch^ (0,grow^[5])
let^ g=f^ ->i64^ | E.Bad { var^ m,ws=try^ count(2) ws.push(1) return^ m+ws.len }
print^(k,ys.len,ys[1],j,zs[0],g() catch^ -1)
let^ forward=f^ n:i64^ ->grow^ i64^[] | E.Bad { return^ try^ make(n) }
let^ settle=f^ n:i64^ ->grow^ i64^[] { return^ make(n) catch^ grow^[0] }
let^ f3=forward(3) catch^ grow^[9]
let^ s1=settle(-1)
let^ s6=settle(6)
print^(f3[1],s1.len,s6[1])
''', '12 -1\n3 1 2 7\n2 9\n1 2\n2 3\n3\n200000\ntrue true\n6 2 30 0 5 7\n4 1 7\n')

    positive('multiple_owned_results', '''
let^ count=f^ n:i64^ ->(i64^,grow^ i64^[]) {
 var^ xs:grow^ i64^[]=[]
 xs.push(n) xs.push(n+1)
 return^ n*2,xs
}
let^ literal=f^ ->(i64^,grow^ i64^[]) { return^ 1,grow^[7,8] }
var^ k,ys=count(5)
ys.push(100)
print^(k,ys.len,ys[2])
repeat^ 3 { let^ _^,zs=count(1) print^(zs[1]) }
let^ one,eight=literal()
print^(one,eight[1])
''', '10 3 100\n2\n2\n2\n1 8\n')

    positive('range_values', '''
let^ xs=[10,20,30,40,50]
let^ head=0 :: 3
let^ tail=(3 ::)
let^ both=1 to^ 3
let^ all=(::)
let^ a=xs[head]
let^ b=xs[tail]
let^ c=xs[both]
let^ d=xs[all]
print^(a.len,a[2],b.len,b[0],c.len,c[2],d.len)
print^(head.start,head.len,both.len,tail.end ?? -1,head.end ?? -1)
print^(head.contains(2),head.contains(3),tail.contains(1000),tail.contains(2))
let^ pick=f^ v:i32^[:],r:range^ ->i32^[:] { v[r] }
let^ ys=xs[::]
print^(pick(ys,2 :: 4)[1],pick(ys,:: 2).len)
let^ e=3 to^ 2
print^(e.len,xs[e].len)
for^ 2 { when^ 1 to^ 3: print^(it^) other^: print^(0) }
''', '3 30 2 40 3 40 5\n0 3 3 -1 3\ntrue false true false\n40 2\n0 0\n2\n')

    positive('chars', '''
let^ a=char^"あ"
let^ b=char^'a'
let^ e=char^"\\u{1F600}"
print^(a,b,e,u32^(a),u32^(e),a=char^"あ",b<a,a<=>b)
let^ c:char^=char^(0x41) catch^ char^"?"
let^ d=char^(0xD800) catch^ char^"?"
let^ f=char^("x") catch^ char^"?"
let^ g=char^("xy") catch^ char^"?"
print^(c,d,f,g)
do^{ let^ h=try^ char^(-1) print^(h) catch^: print^(it^.message()) }
let^ arr:char^[3]=[char^"x",char^"y",char^"z"]
let^ next=f^ c:char^ ->char^ { char^(i64^(c)+1) catch^ c }
print^(arr[1],i64^(arr[2]),next(char^"m"))
''', 'あ a 😀 12354 128512 true true 1\nA ? x ?\nnot a Unicode scalar value\ny 122 n\n')

    positive('strings_utf8', '''
let^ s="aあ😀b"
print^(s.bytes.len,s.count_chars(),s.bytes[0])
var^ at:i64^=0
repeat^ while^ at<s.bytes.len {
 let^ c,next=s.decode(at)
 print^(c,s.slice(at :: next))
 at:=next
}
let^ cs=s.chars()
print^(cs.len,cs[1],cs[2])
print^(s.find("😀") ?? -1,s.find("z") ?? -1,s.starts_with("aあ"),s.ends_with("b"),s.ends_with("x"))
print^("abc"="abc","abc"<"abd","b">"abc","a"<=>"b","x"!="y")
let^ raw:u8^[3]=[104,105,33]
let^ t=string^(raw[::]) catch^ "?"
let^ bad:u8^[2]=[255,1]
let^ u=string^(bad[::]) catch^ "invalid"
print^(t,u,s.slice(1 :: 4),s.slice((4 ::)))
''', '9 4 97\na a\nあ あ\n😀 😀\nb b\n4 あ 😀\n4 -1 true true false\ntrue true true -1 true\nhi! invalid あ 😀b\n')

    positive('tostring', '''
import^ std.text
let^ a=(42).tostring()
let^ n:u64^=18446744073709551615
let^ c=n.tostring()
let^ d=(0.1).tostring()
let^ e=(2.0).tostring()
let^ g=false^.tostring()
let^ h=char^"あ".tostring()
let^ i="text".tostring()
print^(a.text(),c.text(),d.text(),e.text(),g.text(),h.text(),i.text())
var^ out=std.text.Builder{}
let^ x:i128^=-170141183460469231731687303715884105728
x.write_text(ref^ out)
out.append(" / ")
out.append_char(char^"😀")
out.append_char(char^"é")
(3.25).write_text(ref^ out)
let^ small:f16^=1.5
small.write_text(ref^ out)
print^(out.text())
out.clear()
print^(out.text().bytes.len)
print^(2500.0,1.0/3.0,1.5e-7,1e21,123456789012345680000.0,-0.0,100.5)
let^ z=0.0
print^(1.0/z,-1.0/z,z/z)
''', '42 18446744073709551615 0.1 2.0 false あ text\n-170141183460469231731687303715884105728 / 😀é3.251.5\n0\n2500.0 0.3333333333333333 1.5e-07 1e+21 123456789012345680000.0 -0.0 100.5\ninf -inf nan\n')

    positive('loops', '''
var^ total=0
for^ var^ i=0 while^ i<5 next^ i+=1 { if^ i=2 { next^ } total+=i }
print^(total)
for^ i from^ 2 to^ 8 step^ 3 { print^(i) }
for^ i:i8^ from^ 126 to^ 127 { print^(i) }
for^ i:u8^ from^ 2 downto^ 0 { print^(i) }
for^ 4 to^ 5 { print^(it^) }
repeat^ 3 { total+=1 }
print^(total)
repeat^ { repeat^ { break^^ } }
var^ n=0
repeat^ until^ n=3 { n+=1 }
repeat^ while^ n<4 { n+=1 }
print^(n)
var^ a=0 var^ b=0
for^ a:=0 while^ a<2 next^ a+=1,b+=2 { }
print^(a,b)
for^ var^ x=0 for^ var^ y=0 while^ x<2 next^ x+=1,y+=1 { print^(x,y) }
for^ var^ c=0 for^ var^ d=0 while^ c<3 next^ c,d+=1,2 { print^(c,d) }
for^ var^ e=0 while^ e<1 next^ e+=1,print^(e) {}
''', '8\n2\n5\n8\n126\n127\n2\n1\n0\n4\n5\n11\n4\n2 4\n0 0\n1 1\n0 0\n1 2\n2 4\n1\n')

    positive('counted_extrema', '''
for^ i:u128^ from^ 340282366920938463463374607431768211454 to^ 340282366920938463463374607431768211455 { print^(i) }
for^ i:i128^ from^ -170141183460469231731687303715884105728 to^ -170141183460469231731687303715884105727 { print^(i) }
for^ i:i8^ from^ -127 to^ 127 step^ 127 { print^(i) }
for^ i from^ 9 to^ 1 { print^(99) }
for^ i from^ 1 downto^ 9 { print^(98) }
''', '340282366920938463463374607431768211454\n340282366920938463463374607431768211455\n-170141183460469231731687303715884105728\n-170141183460469231731687303715884105727\n-127\n0\n127\n')

    positive('for_range_proof', '''
var^ values:i32^[4]=[1,2,3,4]
var^ total=0
for^ var^ i:i64^=0 while^ i<4 next^ i+=1 {total+=values[i]}
print^(total)
''', '10\n', inspect=versioned)

    positive('sections', '''
var^ calls=0
for^ var^ i=0 while^ i<3 next^ i+=1 {
prolog^: print^(10)
pre^: var^ temp:grow^ i32^[]=[] temp.push(i) calls+=1
first^: var^ kept:grow^ i32^[]=[] kept.push(temp[0])
main^: kept.push(i) print^(temp[0])
last^: print^(i) print^(kept.len)
epilog^: print^(20)
}
print^(calls)
repeat^ 0 { pre^: print^(30) first^: print^(99) last^: print^(98) epilog^: print^(40) }
repeat^ 3 { main^: var^ tmp:grow^ i32^[]=[] tmp.push(1) break^ last^: print^(50) epilog^: print^(60) }
repeat^ 2 { first^: let^ a,b=1,2 main^: print^(a,b) last^: print^(a+b) }
''', '10\n0\n1\n2\n2\n4\n20\n4\n30\n40\n50\n60\n1 2\n1 2\n3\n')

    positive('jumps_cleanup', '''
var^ hits=0
for^ var^ i=0 while^ i<3 next^ i+=1 {
main^:
 repeat^ 2 {
 main^: var^ owned:grow^ i32^[]=[] owned.push(i) hits+=1 continue^[2]
 last^: print^(99)
 epilog^: print^(98)
 }
last^: print^(i)
epilog^: print^(hits)
}
repeat^ 2 {
main^: repeat^ 2 { main^: break^[2] last^: print^(97) epilog^: print^(96) }
last^: print^(1)
epilog^: print^(2)
}
let^ early=p^ ->i32^ {
 repeat^ 2 { first^: var^ a:grow^ i32^[]=[] main^: a.push(42) return^ a[0] last^: print^(95) epilog^: print^(94) }
 return^ 0
}
print^(early())
''', '2\n3\n1\n2\n42\n')

    positive('section_paths', '''
repeat^ 2 { print^(1) epilogue^: print^(2) }
repeat^ 0 { prologue^: print^(3) premain^: print^(4) epilogue^: print^(5) }
repeat^ 1 {
main^:
 repeat^ 1 { pre^: break^^ first^: var^ a:grow^ i32^[]=[] main^: a.push(0) }
last^: print^(6)
epilog^: print^(7)
}
repeat^ 1 {
pre^: break^
first^: var^ a:grow^ i32^[]=[]
main^: a.push(99)
last^: print^(98)
epilog^: print^(8)
}
let^ early=p^ ->i32^ { repeat^ 1 { pre^: return^ 9 main^: print^(97) } }
print^(early())
repeat^ 1 {first^: let^ _^,_^=1,2 main^: print^(10)}
let^ limit:i64^=2
for^ i from^ 1 to^ limit { print^(i) }
var^ negative=-2
for^ i from^ 0 to^ 2 step^ -negative { print^(i) }
''', '1\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n1\n2\n0\n2\n')

    positive('value_matches', '''
let^ classify=f^ x:i32^ { for^ x: when^ 0: 10 when^ 1 to^ 3,7: 20 other^: 30; }
print^(classify(0),classify(2),classify(7),classify(9))
for^ 4 { when^ 0,1: print^(99) when^ 2 to^ 5: print^(it^) other^: print^(98) }
for^ let^ value=2 { when^ 2: print^(value) }
print^(for^ false^: when^ true^: 1 when^ false^: 2;)
print^(for^ let^ n=4 if^ n>2: n*2 el^: 0;)
print^(for^ let^ n=5 do^: n+1;)
for^ let^ n=2 if^ n>1 { print^(n) else^: print^(0) }
''', '10 20 20 30\n4\n2\n2\n8\n6\n2\n')

    positive('lexical_scopes', '''
var^ x=1
do^{ var^ x=2 do^{ var^ x=3 print^($x,$$x,$$$x,$^x,$^^x) $^^x+=10 } }
print^(x)
let^ select=f^ x:i32^ { do^{ let^ x:i64^=9 return^ $^x } }
print^(select(42))
for^ 1 to^ 2 { for^ 3 to^ 4 { print^(it^,it^^) } }
let^ _^=1 let^ _^=2 let^ _^,y,_^=3,4,5
_^,x := 0,y
print^(x)
let^ ignored=p^ _^:i32^,_^:i64^ { print^(42) }
ignored(1,2)
''', '1 2 3 2 1\n11\n42\n3 1\n4 1\n3 2\n4 2\n4\n42\n')

    positive('hidden_names', '''
let^ `@counter1`=99
for^ i from^ 0 to^ 1 { print^(`@counter1`) }
let^ `@match2`=88
print^(for^ 1: when^ 1: `@match2` other^: 0;)
''', '99\n99\n88\n')

    positive('literals', r'''
let^ 変数=0xFF+0o10+0b11+1_000
let^ `not a name`=7 let^ `a``b`=8
print^(変数,`not a name`,`a``b`)
let^ s="A\0B\x43\u{1f600}"
print^(s.bytes.len,s.bytes[1],s.bytes[2])
var^ bytes:grow^ u8^[]=[] bytes.append("a\0b")
print^(bytes.len,bytes[1],bytes[2])
print^('a''b')
print^("x
z")
let^ raw="""a\b
print^(raw)
print^(1.2_5e+1)
print^("or^","==","=/")
''', "1266 7 8\n8 0 66\n3 0 98\na'b\nx\nz\na\\b\n12.5\nor^ == =/\n")

    source('syntaxlib', '''module^ syntaxlib
public^ let^ left,right=20,22
public^ let^ select=template^f^ value { for^ value: when^ 1: left+right other^: 0; }
''')
    positive('module_syntax', 'import^ syntaxlib\nprint^(syntaxlib.select(1))', '42\n')
    source('discardlib', 'module^ discardlib\nlet^ _^=print^(1)')
    source('outerthislib', '''module^ outerthislib
public^ let^ sum=template^f^ n {
 if^ n=0: 0 el^: n+template^f^ m {this^^(m)}(n-1);
}
''')
    positive('outer_this_module', '''import^ outerthislib
let^ wide:i64^=20
print^(outerthislib.sum(10),outerthislib.sum(wide))
''', '55 210\n')

    cases = [
        ('ref_repeated_expression', 'var^ x=1 let^ r=ref^^ x', 'cannot borrow a temporary'),
        ('ref_repeated_expanded_expression', 'var^ x=1 let^ r=ref^ ref^ x', 'cannot borrow a temporary'),

        ('old_ref_expression', 'var^ x=1 let^ r=&x', 'expected an expression'),
        ('old_ref_type', 'let^ f=f^ x:&i32^ {*x}', 'expected a name'),
        ('old_double_ref_type', 'let^ f=f^ x:&&i32^ {**x}', 'expected a name'),

        ('outer_this_top', 'this^^()', 'at that nesting depth'),
        ('outer_this_depth', 'print^(f^ ->i32^ {this^^()}())', 'at that nesting depth'),
        ('outer_this_sibling', 'let^ first=f^ {1} print^(f^ ->i32^ {this^^()}())', 'at that nesting depth'),
        ('outer_this_arity', 'print^(f^ n:i32^ ->i32^ {f^ {this^^()}()}(1))', 'expects 1 arguments'),
        ('outer_this_type', 'print^(f^ n:i32^ ->i32^ {f^ {this^^("bad")}()}(1))', "requires nominal 'i32^'"),
        ('outer_this_capture', 'print^(f^ n:i32^ ->i32^ {f^ {this^^(n-1)}()}(1))', "unknown value 'n'"),
        ('outer_this_effect', 'let^ run=p^ n:i32^ {f^ {this^^(0)}()} run(1)', 'f^ cannot call p^'),
        ('outer_this_local_owner', '''let^ run=f^ n:i32^ ->i32^ {
var^ scratch:grow^ i32^[]=[n]
if^ n=0 {return^ scratch[0]}
return^ f^ m:i32^ ->i32^ {this^^(m-1)}(n)
} print^(run(3))''', 'holds local owned value'),
        ('outer_this_receiver', '''def^ A {run=f^ self^,n:i32^ ->i32^ {f^ {this^^(1,0)}()}}
var^ a=A.new() print^(a.run(1))''', 'explicit'),
        ('outer_this_mutable_effect', '''def^ A {run=f^ mutable^ self^,n:i32^ ->i32^ {
f^ obj:ref^ A ->i32^ {this^^(obj,0)}(self^)
}} var^ a=A.new() print^(a.run(1))''', 'cannot mutate borrowed storage'),
        ('old_else', 'if^ true^ {} else^ {}', 'else^'),
        ('old_while', 'while^ false^ {}', 'driving clause'),
        ('bare_record_no_context', 'let^ p={x=1}', 'requires a single nominal struct'),
        ('bare_block', '{let^ x=1}', 'standalone blocks require do^'),
        ('bare_record_empty_no_context', 'let^ p={}', 'requires a single nominal struct'),
        ('contextual_record_ambiguous', 'def^ A {self^{x:i32^}} def^ B {self^{y:i32^}} let^ a:A|B={x=1}', 'single nominal struct'),
        ('contextual_record_numeric', 'let^ x:i32^={}', 'single nominal struct'),
        ('contextual_record_unknown_field', 'def^ P {self^{x:i32^}} let^ p:P={y=1}', 'unknown field'),
        ('contextual_record_duplicate', 'def^ P {self^{x:i32^}} let^ p:P={x=1,x=2}', 'duplicate field'),
        ('contextual_record_private', 'def^ P {self^{private^ x:i32^=0}} let^ p:P={x=1}', 'private member'),
        ('contextual_record_untyped_template', 'let^ f=template^p^ x {} f({x=1})', 'single nominal struct'),
        ('contextual_record_error_union', 'errordef^ E {Bad} let^ bad:i32^|E.Bad={}', 'contextual construction requires'),
        ('contextual_record_borrow_escape', 'def^ Ref {self^{p:ref^ i32^}} let^ bad=f^ ->Ref {var^ n=1 return^ {p=ref^ n}} let^ v=bad()', 'cannot return a reference'),
        ('let_colon_equal', 'let^ x:=1', "expected '='"),
        ('immutable_compound', 'let^ x=1 x+=1', 'cannot modify let^'),
        ('readonly_focus', 'for^ i from^ 1 to^ 2 {i+=1}', 'cannot modify let^'),
        ('missing_match_default', 'let^ x=for^ 1: when^ 1: 2;', 'requires other^:'),
        ('missing_if_default', 'let^ x=if^ true^: 1;', 'requires a final'),
        ('jump_outside', 'break^', 'jump depth'),
        ('jump_too_far', 'repeat^ { next^^ }', 'jump depth'),
        ('zero_jump_depth', 'repeat^ { break^[0] }', 'must be positive'),
        ('double_jump_depth', 'repeat^ { break^^[2] }', 'not both'),
        ('zero_step', 'for^ i from^ 1 to^ 2 step^ 0 {}', 'must be positive'),
        ('negative_step', 'for^ i from^ 1 to^ 2 step^ -1 {}', 'must be positive'),
        ('float_count', 'repeat^ 1.5 {}', 'integer type'),
        ('counted_update', 'for^ i from^ 1 to^ 2 next^ i+=1 {}', 'explicit update'),
        ('update_jump', 'for^ var^ i=0 while^ i<2 next^ next^ {}', 'assignment or call'),
        ('missing_driver', 'for^ var^ i=0 { print^(i) }', 'driving clause'),
        ('focusless_for', 'for^ while^ false^ {}', 'requires a focus'),
        ('immutable_loop_focus', 'for^ let^ i=0 while^ i<2 {}', 'requires var^'),
        ('double_driver', 'repeat^ 2 while^ false^ {}', 'another driving clause'),
        ('section_order', 'repeat^ 1 {main^: print^(1) first^: print^(2)}', 'sections must follow'),
        ('section_duplicate', 'repeat^ 1 {main^: main^:}', 'sections must follow'),
        ('section_no_body', 'repeat^ 1 {prolog^: print^(1)}', 'requires pre^'),
        ('first_zero_use', 'repeat^ 0 {first^: let^ x=1 epilog^: print^(x)}', 'not definitely initialized'),
        ('first_borrow_pre', 'repeat^ 1 {pre^: var^ x=1 first^: let^ p=ref^ x main^: print^(*p)}', 'outlive'),
        ('scope_escape', 'let^ p=for^ var^ x=1 do^: ref^ x;', 'local owner'),
        ('scope_too_far', 'let^ x=1 print^($^x)', 'outside'),
        ('scope_space', 'let^ x=1 print^($ x)', 'adjacent'),
        ('discard_read', 'let^ _^=1 print^(_^)', 'cannot be read'),
        ('discard_compound', '_^+=1', 'compound assignment'),
        ('duplicate_parallel', 'let^ x,x=1,2', 'duplicate binding'),
        ('parallel_type_collision', 'def^ P {} let^ P,x=1,2', 'conflicts with def^'),
        ('parallel_import_collision', 'import^ std.random let^ std,x=1,2', 'conflicts with import'),
        ('parallel_count', 'var^ x,y=1,2,3', 'does not match'),
        ('tostring_owned', 'let^ f=f^ { (1).tostring().text() }', 'bind it'),
        ('string_no_len', 'let^ s="x"\nprint^(s.len)', 'string^ has no len'),
        ('string_no_index', 'let^ s="x"\nprint^(s[0])', 'string^ has no []'),
        ('string_no_slice', 'let^ s="xy"\nlet^ t=s[0 :: 1]', 'slice it with s.slice'),
        ('string_hex_escape', 'let^ s="\\xFF"', 'stop at \\x7F'),
        ('string_chars_owned', 'let^ n=f^ s:string^ { s.chars().len }', 'bind it'),
        ('char_literal_two', 'let^ a=char^"ab"', 'exactly one Unicode scalar value'),
        ('char_literal_empty', 'let^ a=char^""', 'exactly one Unicode scalar value'),
        ('char_arithmetic', 'let^ a=char^"a"+char^"b"', "operator '+' is not defined for 'char^'"),
        ('char_cast_wide', 'let^ n:u128^=65\nlet^ c=char^(n)', 'at most 64 bits'),
        ('range_open_end', 'let^ r=0 ::\nprint^(r)', 'goes right before ), ] or ,'),
        ('range_open_start', 'let^ r=:: 3', 'goes right after (, [ or ,'),
        ('range_inclusive_open', 'let^ r=(1 to^)', 'needs both bounds'),
        ('range_bounds', 'let^ r=1.5 :: 2.0', 'must be i64^'),
        ('range_member', 'let^ r=0 :: 2\nprint^(r.size)', 'range^ has start, end, len'),
        ('results_not_multiple', 'var^ x,y=1', "'i32^' is not multiple results"),
        ('results_count', 'let^ f=f^ ->(i32^,i32^) {return^ 1,2}\nlet^ a,b,c=f()', '3 names for the 2 results'),
        ('results_assign_count', 'let^ f=f^ ->(i32^,i32^) {return^ 1,2}\nvar^ a,b,c=1,2,3\na,b,c:=f()', '3 names for the 2 results'),
        ('results_one_name', 'let^ f=f^ ->(i32^,i32^) {return^ 1,2}\nlet^ t=f()', 'need a name each'),
        ('results_argument', 'let^ f=f^ ->(i32^,i32^) {return^ 1,2}\nlet^ g=f^ x {0}\nprint^(g(f()))', 'cannot be passed'),
        ('results_single', 'let^ f=f^ ->i32^ {return^ 1,2}', 'multiple values go only where'),
        ('results_tuple_value', 'let^ t=(1,2)', 'need a name each'),
        ('results_union_bare', 'errordef^ E { Bad }\nlet^ f=f^ ->(i32^,i32^) | E.Bad {return^ 1,2}\nlet^ a,b=f()', 'take them out with try^'),
        ('owned_union_copy', 'errordef^ E { Bad }\nlet^ m=f^ ->grow^ i32^[] | E.Bad {return^ grow^[1]}\nlet^ v=m()\nif^ !(v fits^ E.Bad) { let^ w=v }', 'cannot be copied'),
        ('results_short', 'let^ f=f^ ->(i32^,i32^) {return^ 1}', "expected '(i32^, i32^)'"),
        ('results_position', 'let^ f=f^ x:(i32^,i32^) {0}', 'written only after ->'),
        ('results_part_type', 'let^ f=f^ ->(i32^,i32^) {return^ 1,2}\nlet^ a:i64^,b=f()', "result 0 is 'i32^'"),
        ('results_growable_twice', 'let^ f=f^ ->(grow^ i32^[],grow^ i32^[]) {return^ grow^[1],grow^[2]}', 'final field'),
        ('results_owned_moved', 'let^ f=f^ ->(i32^,grow^ i32^[]) {\nvar^ a:grow^ i32^[]=[]\nvar^ b:grow^ i32^[]=[]\nif^ true^ {return^ 1,a}\nreturn^ 2,b\n}\nlet^ n,x=f()', 'cannot be copied'),
        ('duplicate_first', 'repeat^ 1 {first^: let^ x=1 let^ x=2 main^:}', 'duplicate binding'),
        ('prolog_self_jump', 'repeat^ 1 {prolog^: break^ main^:}', 'own loop'),
        ('epilog_self_jump', 'repeat^ 1 {main^: epilog^: next^}', 'own loop'),
        ('first_partial_jump', 'repeat^ 1 {first^: break^ let^ x=1 main^:}', 'before all first^'),
        ('elseif_default', 'if^ false^ { elseif^: print^(1) }', 'requires a condition'),
        ('declared_hat', 'let^ custom^=1', 'cannot be declared'),
        ('c_equality', 'print^(1==1)', 'C-style'),
        ('c_and', 'print^(true^ && false^)', "expected ')'"),
        ('empty_quote', 'let^ ``=1', 'empty or unterminated'),
        ('quote_newline', 'let^ `a\nb`=1', 'newline'),
        ('quote_control', 'let^ `\x01counter1`=1', 'control character'),
        ('module_discard_call', 'import^ discardlib', 'scalar literal operations'),
        ('bad_base', 'print^(0b102)', 'numeric literal'),
        ('bad_separator', 'print^(1__0)', 'separator'),
        ('bad_unicode', r'print^("\u{d800}")', 'Unicode scalar'),
        ('bad_escape', r'print^("\x0q")', 'two digits'),
        ('call_newline', 'print\n(1)', 'unknown value'),
    ]
    for name, text, diagnostic in cases:
        negative(name, text, diagnostic)

    suite.runtime_failure('string_slice_cut', 'let^ s="あい"\nprint^(s.slice(1 :: 3))', 'string slice cuts a character')
    suite.runtime_failure('string_decode_middle', 'let^ s="あ"\nlet^ c,n=s.decode(1)', 'not the start of a character')
    suite.runtime_failure('range_open_len', 'let^ r=(5 ::)\nprint^(r.len)', 'an open range has no length')
    suite.runtime_failure('range_inclusive_overflow', 'let^ r=0 to^ 9223372036854775807', 'the end of a to^ range overflows')
    suite.runtime_failure('range_slice_bounds', 'let^ xs=[1,2]\nlet^ r=1 :: 5\nprint^(xs[r].len)', 'slice range out of bounds')
    suite.runtime_failure('dynamic_step', 'var^ step=0 for^ i from^ 1 to^ 0 step^ step {}', 'assert')
    suite.finish()


if __name__ == '__main__':
    main()
