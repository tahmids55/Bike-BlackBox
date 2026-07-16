a = False
def test(s):
    if s == 'yes':
        global a
        a = True
    if a:
        print("A is true")
    else:
        print("A is false")

test('no')
test('yes')
test('no')
