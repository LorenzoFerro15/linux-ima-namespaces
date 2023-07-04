class Node:

    def __init__(self, id):
        self.childs  = []
        self.id = id
        self.parent = None
        self.status = 'active'

    def add_child(self, child):
        self.childs.append(child)
        child.parent = self
        return child
    
    def add_child_between(self, parent, child):
        if(parent == None):
            return None
        parent.add_child(child)
        return
    
    def find_in_childs(self, value):
        found = None
        if self.id == value:
            return self 
        for i in self.childs:
            if i.id == value:
                return i
            found = i.find_in_childs(value)
            if found != None:
                return found
        return found
    
    def find_in_tree(self, value):
        if(self.__find_in_childs(value) != None):
            return True
        else:
            return False
        
    def node_heigh(self, node):
        heigh = 0
        start = node
        while start != None:
            heigh += 1
            start = start.parent
        return heigh
    
    def close_ns(self, value):
        node_to_close = self.find_in_childs(value)
        if node_to_close == None:
            return -1
        node_to_close.status = 'close'
    
# def main():
#     tree = Node(1)
#     child1 = Node(2)
#     child2 = Node(3)
#     child4 = Node(4)
#     child3 = Node(5)

#     tree.add_child_between(tree, child1)
#     tree.add_child_between(child1, child2)
#     tree.add_child_between(child1, child3)
#     tree.add_child_between(child2, child4)

#     return

# if __name__ == "__main__":
#     main()