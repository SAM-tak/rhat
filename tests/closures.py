"""Lexical captures, callback forwarding, and rejection of escaping environments."""
from harness import Suite


def main():
    suite = Suite('closures')
    positive, negative = suite.positive, suite.negative
    positive('captured_argument_read', '''
let^outer=f^n:i32^ ->i32^ {
    let^local=f^ ->i32^ {n}
    return^local()
}
print^(outer(1))
''', '1\n')
    positive('entry_block_capture', '''
let^apply=p^cb:p^; {cb()}
do^{
    var^n=1
    let^step=p^ {n+=1}
    apply(step)
    print^(n)
}
''', '2\n')
    positive('entry_inline_capture', '''
let^apply=p^cb:p^; {cb()}
var^n=1
apply(p^ {n+=1})
print^(n)
''', '2\n')
    positive('local_capture', '''
let^run=p^ {
    var^count=0
    let^step=p^ {count+=1}
    let^other=p^ {count+=10}
    step() step()
    other()
    print^(count)
}
run() run()
''', '12\n12\n')
    positive('callback_capture', '''
let^twice=p^cb:p^; {cb() cb()}
let^run=p^ {
    var^count=0
    let^step=p^ {count+=1}
    twice(step)
    twice(p^ {count+=2})
    print^(count)
}
run()
''', '6\n')
    positive('mixed_captures', '''
let^apply=p^cb:p^; {cb()}
let^run=p^start:i32^ {
    var^count=start
    let^delta=3
    var^other=1
    let^step=p^ {count+=delta other+=count}
    step() apply(step)
    print^(count,other)
}
run(2)
''', '8 14\n')
    positive('nested_capture', '''
let^run=p^ {
    var^count=0
    let^outer=p^ {
        let^inner=p^ {count+=1}
        inner()
    }
    outer() outer()
    print^(count)
}
run()
''', '2\n')
    positive('let_capture_read', '''
let^run=p^ {
    let^n=7
    let^get=f^ {n+1}
    print^(get())
}
run()
''', '8\n')
    positive('forward_callback', '''
let^apply=p^cb:p^; {cb()}
let^forward=p^cb:p^; {apply(cb)}
let^run=p^ {
    var^n=0
    let^step=p^ {n+=1}
    let^indirect=p^ {forward(step)}
    indirect() forward(indirect)
    print^(n)
}
run()
''', '2\n')
    positive('captured_reference', '''
let^run=p^ {
    var^n=1
    let^r=ref^n
    let^step=p^ {*r+=2}
    step()
    let^get=f^ {ref^n}
    let^view=get();
    *view+=1
    print^(n)
}
run()
''', '4\n')
    positive('captured_owned', '''
let^apply=p^cb:p^; {cb()}
let^run=p^ {
    var^items=i32^[grow^]{1,2}
    let^step=p^ {items.push(3)}
    apply(step)
    let^sum=f^ {items[0]+items[2]}
    print^(items.len,sum())
}
run()
''', '3 4\n')
    positive('captured_self', '''
let^twice=p^cb:p^; {cb() cb()}
def^ A {
    self^{private^n=0}
    run=p^self^,delta:i32^ {
        twice(p^ {self^.n+=delta})
    }
    read=f^self^ {self^.n}
}
var^a=A{}
a.run(3)
    print^(a.read())
''', '6\n')
    positive('capture_recursion', '''
let^run=p^ {
    var^sum=0
    let^add=p^n:i32^ {
        sum+=n
        if^n>0 {this^(n-1)}
    }
    add(4)
    print^(sum)
}
run()
''', '10\n')
    positive('capture_shadow', '''
let^run=p^ {
    var^n=1
    let^step=p^ {n+=1}
    do^{var^n=90 step() print^(n)}
    print^(n)
}
run()
''', '90\n2\n')
    positive('captured_argument_callback', '''
let^apply=p^cb:p^i32^;,x:i32^ {cb(x)}
let^run=p^ {
    var^n=1
    let^step=p^delta {n+=delta}
    apply(step,4)
    print^(n)
}
run()
''', '5\n')
    positive('capture_generic_method', '''
def^Box<T> {
    self^{n:T}
    run=p^self^,value:T {
        let^set=p^ {self^.n:=value}
        set()
    }
}
var^a=Box<i32^>{n=1}
var^b=Box<f64^>{n=1.0}
a.run(4) b.run(2.5)
print^(a.n,b.n)
''', '4 2.5\n')
    positive('capture_top_and_local', '''
var^global=0
let^root=p^ {global+=1}
let^apply=p^cb:p^; {cb()}
let^run=p^ {
    var^n=2
    apply(p^ {global+=n root() n+=1})
    print^(n)
}
run()
print^(global)
''', '3\n3\n')
    positive('capture_outer_this', '''
let^count=f^n:i32^ ->i32^ {
    if^n=0 {return^0}
    return^(f^ {this^^(n-1)+1})()
}
print^(count(3))
''', '3\n')
    positive('capture_specializations', '''
let^run=template^p^value {
    let^get=f^ {value}
    print^(get())
}
run(3) run("text")
''', '3\ntext\n')
    negative('capture_pure_callback_call', '''
let^apply=f^cb:p^; {cb()}
let^run=p^ {var^n=0 apply(p^ {n+=1})}
run()
''', 'f^ cannot call p^')
    negative('capture_store_shorter_reference', '''
let^run=p^ {
    var^n=0
    var^r:ref^i32^=ref^n
    let^set=p^ {
        var^local=1
        r:=ref^local
    }
    set()
}
run()
''', 'reference would outlive its owner')
    negative('capture_read_borrow', '''
let^run=p^ {
    var^n=0
    let^step=p^ {n+=1}
    let^r:ref^read^i32^=ref^n
    step()
    print^(*r)
}
run()
''', 'read^ borrow')
    negative('callback_read_borrow', '''
let^apply=p^cb:p^; {cb()}
let^run=p^ {
    var^n=0
    let^step=p^ {n+=1}
    let^r:ref^read^i32^=ref^n
    apply(step)
    print^(*r)
}
run()
''', 'read^ borrow')
    negative('captured_callback_return', '''
let^identity=f^cb:p^; {cb}
let^run=p^ {var^n=0 let^step=p^ {n+=1} let^bad=identity(step)}
run()
''', 'capturing function cannot escape')
    negative('capture_flex_reference', '''
let^run=p^ {
    var^items=i32^[flex^]{1,2}
    let^r=ref^items[0]
    let^read=f^ {*r}
    items.clear()
    print^(read())
}
run()
''', 'may refer into it')
    negative('callback_flex_shrink', '''
let^apply=p^cb:p^; {cb()}
let^run=p^ {
    var^items=i32^[flex^]{1,2}
    let^clear=p^ {items.clear()}
    let^r=ref^items[0]
    apply(clear)
    print^(*r)
}
run()
''', 'may refer into it')
    negative('capture_let_owned_write', '''
let^run=p^ {
    let^items=i32^[grow^]{1,2}
    let^bad=p^ {items.push(3)}
    bad()
}
run()
''', "cannot modify let^ binding 'items'")
    negative('capture_let_borrow', '''
let^run=p^ {
    let^items=i32^[2]{1,2}
    let^bad=p^ {let^r=ref^items print^r[0]}
    bad()
}
run()
''', "requires var^, found let^ 'items'")
    negative('let_capture_write', '''
let^run=p^ {let^n=0 let^bad=p^ {n+=1} bad()}
run()
''', "cannot modify let^ binding 'n'")
    negative('pure_capture_write', '''
let^run=p^ {var^n=0 let^bad=f^ {n+=1} bad()}
run()
''', 'f^ cannot mutate')
    negative('capture_return', '''
let^make=p^ {
    var^n=0
    return^p^ {n+=1}
}
let^bad=make()
''', 'capturing function cannot escape')
    negative('captured_parameter_return', '''
let^make=f^x:i32^ {
    return^f^ {x+1}
}
let^bad=make(5)
''', 'capturing function cannot escape')
    suite.finish()


if __name__ == '__main__':
    main()
