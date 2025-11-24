"""
Example usage of the decision tree web UI
"""
from decision_graph.decision_tree.capi.c_abc import LogicNode, LogicGroup
from decision_graph.decision_tree.webui.main import show


def create_sample_tree():
    """Create a sample decision tree for demonstration."""
    # Create root node
    root = LogicNode("Root")
    
    # Create some child nodes
    child1 = LogicNode("Child1")
    child2 = LogicNode("Child2")
    
    # Add children to root
    root.append(child1)
    root.append(child2)
    
    # Create more nodes for child1
    grandchild1 = LogicNode("GrandChild1")
    grandchild2 = LogicNode("GrandChild2")
    child1.append(grandchild1)
    child1.append(grandchild2)
    
    # Create more nodes for child2
    grandchild3 = LogicNode("GrandChild3")
    child2.append(grandchild3)
    
    # Example with LogicGroup (if available in the actual implementation)
    # This demonstrates the concept, though the actual implementation might differ
    return root


if __name__ == "__main__":
    sample_tree = create_sample_tree()
    print("Starting decision tree visualization...")
    show(sample_tree, host="127.0.0.1", port=5000, open_browser=True, duration=300)